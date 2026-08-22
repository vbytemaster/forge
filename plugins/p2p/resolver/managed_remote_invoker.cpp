module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

module forge.plugins.p2p.resolver.plugin;

import forge.api.core.connection;
import forge.api.core.descriptor;
import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.transport.connection;
import forge.asio.notification;
import forge.exceptions;
import forge.net.p2p.identity;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;

#include "details/plugin_impl.hxx"
#include "details/managed_remote_invoker.hxx"

namespace forge::plugins::p2p::resolver::detail {
namespace {

[[nodiscard]] std::exception_ptr remote_stopped_failure() noexcept {
   try {
      FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
   } catch (...) {
      return std::current_exception();
   }
}

} // namespace

struct managed_remote_invoker::generation {
   std::shared_ptr<forge::api::transport::connection> connection;
   std::shared_ptr<forge::api::core::remote_invoker> invoker;
   forge::api::core::api_ref selected;
   std::size_t peer_index = 0;
};

struct managed_remote_invoker::reconnect_flight {
   explicit reconnect_flight(boost::asio::any_io_executor executor)
       : executor{boost::asio::make_strand(std::move(executor))} {}

   boost::asio::any_io_executor executor;
   forge::asio::notification completed;
   boost::asio::cancellation_signal cancellation;
   std::shared_ptr<generation> result;
   std::exception_ptr error;
   std::size_t waiters = 0;
   bool done = false;
};

struct managed_remote_invoker::timer_state {
   timer_state(boost::asio::any_io_executor executor, std::chrono::milliseconds delay)
       : timer{std::move(executor), delay} {}

   boost::asio::steady_timer timer;
};

managed_remote_invoker::managed_remote_invoker(std::weak_ptr<plugin::impl> owner,
                                               std::vector<forge::net::p2p::peer_id> ordered_peers,
                                               forge::api::core::api_ref requested,
                                               forge::api::core::descriptor descriptor, managed_remote_options options,
                                               std::size_t max_waiters)
    : owner_{std::move(owner)}, peers_{std::move(ordered_peers)}, requested_{std::move(requested)},
      descriptor_{std::move(descriptor)}, options_{options}, max_waiters_{max_waiters} {
   if (peers_.empty() || options_.max_connect_rounds == 0 || options_.max_connect_rounds > 64 ||
       options_.initial_backoff.count() <= 0 || options_.max_backoff < options_.initial_backoff ||
       options_.max_backoff > std::chrono::hours{1} || max_waiters_ == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_remote, "managed remote options are invalid");
   }

   auto unique = std::set<std::string>{};
   for (const auto& peer : peers_) {
      if (!forge::net::p2p::valid_peer_id(peer) || !unique.insert(peer.value).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_remote, "managed remote peer list is invalid");
      }
   }
}

managed_remote_invoker::~managed_remote_invoker() {
   request_stop();
}

boost::asio::awaitable<void> managed_remote_invoker::connect_initial() {
   static_cast<void>(co_await require_generation());
}

void managed_remote_invoker::request_stop() noexcept {
   auto connection = std::shared_ptr<forge::api::transport::connection>{};
   auto timer = std::shared_ptr<timer_state>{};
   auto reconnect = std::shared_ptr<reconnect_flight>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (stopped_) {
         return;
      }
      stopped_ = true;
      if (current_) {
         connection = current_->connection;
         current_.reset();
      }
      timer = std::move(backoff_timer_);
      reconnect = reconnect_;
   }
   if (reconnect) {
      try {
         boost::asio::dispatch(reconnect->executor, [reconnect, timer, connection] {
            try {
               reconnect->cancellation.emit(boost::asio::cancellation_type::all);
               if (timer) {
                  static_cast<void>(timer->timer.cancel());
               }
               if (connection) {
                  connection->cancel();
               }
            } catch (...) {
            }
         });
      } catch (...) {
      }
   } else if (connection) {
      connection->cancel();
   }
}

boost::asio::awaitable<void> managed_remote_invoker::async_stop() {
   request_stop();
   auto reconnect = std::shared_ptr<reconnect_flight>{};
   auto observed = forge::asio::notification::epoch_type{};
   {
      auto lock = std::scoped_lock{mutex_};
      reconnect = reconnect_;
      if (!reconnect || reconnect->done) {
         co_return;
      }
      observed = reconnect->completed.epoch();
   }
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   static_cast<void>(co_await reconnect->completed.async_wait(observed));
}

bool managed_remote_invoker::stopped() const noexcept {
   auto lock = std::scoped_lock{mutex_};
   return stopped_;
}

std::chrono::milliseconds managed_remote_invoker::backoff_for(std::uint32_t round) const noexcept {
   auto value = options_.initial_backoff;
   for (auto index = std::uint32_t{0}; index < round && value < options_.max_backoff; ++index) {
      value = std::min(options_.max_backoff, value * 2);
   }
   return value;
}

boost::asio::awaitable<std::shared_ptr<managed_remote_invoker::generation>>
managed_remote_invoker::require_generation() {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto reconnect = std::shared_ptr<reconnect_flight>{};
   auto stale = std::shared_ptr<forge::api::transport::connection>{};
   auto observed = forge::asio::notification::epoch_type{};
   auto start = false;
   {
      auto lock = std::scoped_lock{mutex_};
      if (stopped_) {
         FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
      }
      if (current_ && current_->connection->valid()) {
         co_return current_;
      }
      if (current_) {
         stale = current_->connection;
         next_peer_ = (current_->peer_index + 1U) % peers_.size();
         current_.reset();
      }
      reconnect = reconnect_;
      if (!reconnect) {
         reconnect = std::make_shared<reconnect_flight>(executor);
         reconnect_ = reconnect;
         start = true;
      }
      if (reconnect->waiters >= max_waiters_) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                               "managed remote reconnect waiter limit exceeded");
      }
      ++reconnect->waiters;
      observed = reconnect->completed.epoch();
   }
   if (stale) {
      stale->cancel();
   }

   if (start) {
      auto self = shared_from_this();
      boost::asio::co_spawn(
          reconnect->executor, self->run_connect(reconnect),
          boost::asio::bind_cancellation_slot(reconnect->cancellation.slot(),
                                              [self = std::move(self)](std::exception_ptr) noexcept {}));
   }

   try {
      static_cast<void>(co_await reconnect->completed.async_wait(observed));
   } catch (const boost::system::system_error& error) {
      leave_flight(reconnect);
      if (error.code() == boost::asio::error::operation_aborted) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "managed remote reconnect wait was cancelled");
      }
      throw;
   } catch (...) {
      leave_flight(reconnect);
      throw;
   }

   auto result = std::shared_ptr<generation>{};
   auto error = std::exception_ptr{};
   auto was_stopped = false;
   {
      auto lock = std::scoped_lock{mutex_};
      was_stopped = stopped_;
      result = reconnect->result;
      error = reconnect->error;
   }
   leave_flight(reconnect);
   if (was_stopped) {
      FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
   }
   if (error) {
      std::rethrow_exception(error);
   }
   if (!result) {
      FORGE_THROW_EXCEPTION(exceptions::remote_unavailable, "managed remote reconnect completed without a generation");
   }
   co_return result;
}

boost::asio::awaitable<void> managed_remote_invoker::run_connect(std::shared_ptr<reconnect_flight> flight) {
   auto result = std::shared_ptr<generation>{};
   auto error = std::exception_ptr{};
   try {
      result = co_await connect_generation();
   } catch (...) {
      error = std::current_exception();
   }
   const auto stopped_error = result ? remote_stopped_failure() : std::exception_ptr{};
   auto canceled = std::shared_ptr<forge::api::transport::connection>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (result && (stopped_ || reconnect_ != flight)) {
         canceled = result->connection;
         result.reset();
         if (!error) {
            error = stopped_error;
         }
      } else if (result) {
         current_ = result;
         next_peer_ = result->peer_index;
      }
      flight->result = std::move(result);
      flight->error = std::move(error);
      flight->done = true;
      if (reconnect_ == flight) {
         reconnect_.reset();
      }
   }
   if (canceled) {
      canceled->cancel();
   }
   flight->completed.notify();
}

boost::asio::awaitable<std::shared_ptr<managed_remote_invoker::generation>>
managed_remote_invoker::connect_generation() {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto start = std::size_t{};
   {
      auto lock = std::scoped_lock{mutex_};
      start = next_peer_;
   }

   for (auto round = std::uint32_t{0}; round < options_.max_connect_rounds; ++round) {
      for (auto offset = std::size_t{0}; offset < peers_.size(); ++offset) {
         const auto index = (start + offset) % peers_.size();
         const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
         if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "managed remote connect was cancelled");
         }
         if (stopped()) {
            FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
         }

         auto owner = owner_.lock();
         if (!owner) {
            FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote owner is unavailable");
         }
         try {
            auto opened =
                co_await owner->open_resolved_connection(peers_[index], requested_, descriptor_, options_.resolution);
            auto connection = std::make_shared<forge::api::transport::connection>(std::move(opened.connection));
            auto invoker = co_await connection->get_remote_invoker(opened.selected, descriptor_);
            auto result = std::make_shared<generation>(generation{
                .connection = std::move(connection),
                .invoker = std::move(invoker),
                .selected = std::move(opened.selected),
                .peer_index = index,
            });
            co_return result;
         } catch (const exceptions::remote_stopped&) {
            throw;
         } catch (const forge::api::core::exceptions::cancelled&) {
            if (stopped()) {
               FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
            }
            if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
               throw;
            }
         } catch (const forge::exceptions::base&) {
            if (stopped()) {
               FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
            }
         }
      }

      if (round + 1U < options_.max_connect_rounds) {
         auto timer = std::make_shared<timer_state>(executor, backoff_for(round));
         {
            auto lock = std::scoped_lock{mutex_};
            if (stopped_) {
               FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
            }
            backoff_timer_ = timer;
         }
         auto error = boost::system::error_code{};
         co_await timer->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         {
            auto lock = std::scoped_lock{mutex_};
            if (backoff_timer_ == timer) {
               backoff_timer_.reset();
            }
         }
         if (error) {
            if (stopped()) {
               FORGE_THROW_EXCEPTION(exceptions::remote_stopped, "managed remote is stopped");
            }
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "managed remote backoff was cancelled");
         }
      }
   }

   FORGE_THROW_EXCEPTION(exceptions::remote_unavailable, "managed remote could not connect to an ordered peer");
}

void managed_remote_invoker::invalidate(const std::shared_ptr<generation>& value) noexcept {
   auto connection = std::shared_ptr<forge::api::transport::connection>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (current_ != value) {
         return;
      }
      connection = current_->connection;
      next_peer_ = (current_->peer_index + 1U) % peers_.size();
      current_.reset();
   }
   connection->cancel();
}

void managed_remote_invoker::leave_flight(const std::shared_ptr<reconnect_flight>& value) noexcept {
   auto lock = std::scoped_lock{mutex_};
   if (value->waiters != 0) {
      --value->waiters;
   }
}

boost::asio::awaitable<forge::api::core::response> managed_remote_invoker::async_call(forge::api::core::request value) {
   auto generation = co_await require_generation();
   value.api = generation->selected;
   try {
      co_return co_await generation->invoker->async_call(std::move(value));
   } catch (const forge::api::core::exceptions::deadline_exceeded&) {
      throw;
   } catch (const forge::api::core::exceptions::cancelled&) {
      throw;
   } catch (const forge::api::core::exceptions::resource_exhausted&) {
      throw;
   } catch (...) {
      invalidate(generation);
      throw;
   }
}

boost::asio::awaitable<forge::api::core::response>
managed_remote_invoker::async_stream_call(forge::api::core::request value, forge::api::core::method_kind kind,
                                          std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
                                          std::shared_ptr<forge::api::core::detail::stream_endpoint> output) {
   auto generation = co_await require_generation();
   value.api = generation->selected;
   try {
      co_return co_await generation->invoker->async_stream_call(std::move(value), kind, std::move(input),
                                                                std::move(output));
   } catch (const forge::api::core::exceptions::deadline_exceeded&) {
      throw;
   } catch (const forge::api::core::exceptions::cancelled&) {
      throw;
   } catch (const forge::api::core::exceptions::resource_exhausted&) {
      throw;
   } catch (...) {
      invalidate(generation);
      throw;
   }
}

} // namespace forge::plugins::p2p::resolver::detail
