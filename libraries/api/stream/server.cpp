module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>

module forge.api.stream.server;

import forge.raw.raw;
import forge.raw.exceptions;
import forge.asio.gate;
import forge.api.core.error_projection;
import forge.net.transport.exceptions;
import forge.net.transport.frame;

namespace forge::api::stream {
namespace {

constexpr auto compact_threshold = std::size_t{65'536};

void compact_buffer(std::vector<std::uint8_t>& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = std::vector<std::uint8_t>{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

[[nodiscard]] std::span<const std::uint8_t> available_bytes(const std::vector<std::uint8_t>& buffer,
                                                            std::size_t consumed) noexcept {
   if (consumed >= buffer.size()) {
      return {};
   }
   return {buffer.data() + consumed, buffer.size() - consumed};
}

boost::asio::awaitable<forge::net::transport::chunk> read_transport_frame(forge::net::transport::stream& stream,
                                                                          std::vector<std::uint8_t>& buffer,
                                                                          std::size_t& consumed,
                                                                          std::uint32_t max_frame_size) {
   while (true) {
      const auto decoded = forge::net::transport::decode_frame_view(
          available_bytes(buffer, consumed), forge::net::transport::frame_options{.max_size = max_frame_size});
      if (decoded.status == forge::net::transport::frame_decode_status::complete) {
         const auto payload = forge::net::transport::chunk{decoded.payload};
         consumed += decoded.consumed;
         if (consumed >= buffer.size() || consumed > compact_threshold) {
            compact_buffer(buffer, consumed);
         }
         co_return payload;
      }

      compact_buffer(buffer, consumed);
      auto next = co_await stream.async_read_chunk();
      auto view = next.bytes();
      buffer.insert(buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> write_transport_frame(forge::net::transport::stream& stream,
                                                   std::span<const std::uint8_t> payload,
                                                   std::uint32_t max_frame_size) {
   auto encoded = std::vector<std::uint8_t>{};
   forge::net::transport::encode_frame_to(encoded, payload,
                                          forge::net::transport::frame_options{.max_size = max_frame_size});
   co_await stream.async_write(forge::net::transport::chunk{std::move(encoded)});
}

[[nodiscard]] forge::raw::unpack_limits wire_unpack_limits(std::size_t payload_size,
                                                           std::uint32_t max_frame_size) noexcept {
   const auto bounded_size = static_cast<std::uint32_t>(std::min<std::size_t>(payload_size, max_frame_size));
   return forge::raw::unpack_limits{
       .max_container_elements = bounded_size,
       .max_total_container_elements = bounded_size,
       .max_bytes = bounded_size,
       .first_container_elements = bounded_size,
   };
}

[[nodiscard]] forge::api::core::frame decode_wire_frame(forge::net::transport::chunk payload,
                                                        std::uint32_t max_frame_size) {
   try {
      auto encoded = std::move(payload).into_vector();
      return forge::raw::unpack_exact<forge::api::core::frame>(std::span<const std::uint8_t>{encoded},
                                                               wire_unpack_limits(encoded.size(), max_frame_size));
   } catch (const forge::raw::exceptions::allocation_limit&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                            "API stream frame exceeds allocation limits");
   } catch (const std::bad_alloc&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted, "API stream frame allocation failed");
   } catch (const forge::raw::exceptions::range_error&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "API stream frame is malformed");
   } catch (const forge::raw::exceptions::codec_error&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "API stream frame is malformed");
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "API stream frame is malformed");
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "API stream frame is malformed");
   } catch (...) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "API stream frame is malformed");
   }
}

[[nodiscard]] bool is_clean_close(const forge::exceptions::base& error) noexcept {
   return forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::canceled);
}

[[nodiscard]] std::exception_ptr make_internal_failure(const char* message) noexcept {
   try {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::remote_internal, message);
   } catch (...) {
      return std::current_exception();
   }
}

struct server_state : std::enable_shared_from_this<server_state> {
   using strand_type = boost::asio::strand<boost::asio::any_io_executor>;

   struct active_call {
      explicit active_call(const strand_type& executor) : deadline(executor) {}

      std::shared_ptr<boost::asio::cancellation_signal> cancellation =
          std::make_shared<boost::asio::cancellation_signal>();
      boost::asio::steady_timer deadline;
      bool cancelled = false;
      bool dispatching = true;
   };

   server_state(forge::net::transport::stream stream_value, forge::api::core::binding_plan plan, options options_value,
                forge::api::core::metadata trusted_metadata, strand_type executor_value)
       : stream(std::move(stream_value)), dispatcher(std::move(plan),
                                                     forge::api::core::dispatch_options{
                                                         .codec = options_value.codec,
                                                         .max_inflight = options_value.max_inflight,
                                                         .deadline = options_value.deadline,
                                                         .trusted_metadata = std::move(trusted_metadata),
                                                     }),
         settings(options_value), executor(std::move(executor_value)),
         completion(executor, boost::asio::steady_timer::time_point::max()) {}

   void fail(std::exception_ptr value) noexcept {
      if (!failure) {
         failure = value ? std::move(value) : make_internal_failure("API stream server failed");
      }
      closed = true;
      writer.close();
      for (auto& [_, call] : active) {
         call->cancelled = true;
         call->deadline.cancel();
         call->cancellation->emit(boost::asio::cancellation_type::all);
      }
      active.clear();
      completion.cancel();
      stream.cancel();
   }

   void stop() noexcept {
      closed = true;
      writer.close();
      for (auto& [_, call] : active) {
         call->cancelled = true;
         call->deadline.cancel();
         call->cancellation->emit(boost::asio::cancellation_type::all);
      }
      active.clear();
      completion.cancel();
      stream.cancel();
   }

   void erase_call(forge::api::core::call_id id, const std::shared_ptr<active_call>& call) noexcept {
      const auto found = active.find(id.value);
      if (found != active.end() && found->second == call) {
         call->deadline.cancel();
         active.erase(found);
         if (active.empty()) {
            completion.cancel();
         }
      }
   }

   void finish_call_task() noexcept {
      if (outstanding_call_tasks == 0) {
         fail(make_internal_failure("API stream server call task accounting underflow"));
         return;
      }
      --outstanding_call_tasks;
      completion.cancel();
   }

   boost::asio::awaitable<void> cancel_call(forge::api::core::frame value) {
      auto found = active.find(value.id.value);
      auto call = found == active.end() ? std::shared_ptr<active_call>{} : found->second;
      auto acknowledgement = value;
      static_cast<void>(dispatcher.cancel(std::move(value)));
      if (call) {
         call->cancelled = true;
         call->deadline.cancel();
         active.erase(found);
         if (active.empty()) {
            completion.cancel();
         }
         if (call->dispatching) {
            call->cancellation->emit(boost::asio::cancellation_type::all);
         }
      }
      acknowledgement.kind = forge::api::core::frame_kind::cancel;
      acknowledgement.payload.clear();
      auto ticket = co_await writer.acquire();
      auto encoded = forge::api::core::bytes{};
      forge::raw::pack(encoded, acknowledgement);
      co_await write_transport_frame(stream, encoded, settings.max_frame_size);
   }

   [[nodiscard]] forge::api::core::frame deadline_response(const forge::api::core::frame& request) const {
      auto response = request;
      response.kind = forge::api::core::frame_kind::error;
      response.payload.clear();
      forge::raw::pack(response.payload,
                       forge::api::core::make_core_error_payload(forge::api::core::exceptions::code::deadline_exceeded,
                                                                 "API stream server call deadline exceeded"));
      return response;
   }

   boost::asio::awaitable<void> watch_deadline(forge::api::core::frame request,
                                               const std::shared_ptr<active_call>& call) {
      call->deadline.expires_after(settings.deadline);
      auto error = boost::system::error_code{};
      co_await call->deadline.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error == boost::asio::error::operation_aborted) {
         co_return;
      }
      if (error) {
         fail(make_internal_failure("API stream server deadline timer failed"));
         co_return;
      }

      const auto found = active.find(request.id.value);
      if (found == active.end() || found->second != call || call->cancelled || !call->dispatching) {
         co_return;
      }

      auto cancellation = request;
      cancellation.kind = forge::api::core::frame_kind::cancel;
      cancellation.payload.clear();
      static_cast<void>(dispatcher.cancel(std::move(cancellation)));
      call->cancelled = true;
      active.erase(found);
      if (active.empty()) {
         completion.cancel();
      }
      call->cancellation->emit(boost::asio::cancellation_type::all);

      try {
         auto ticket = co_await writer.acquire();
         auto encoded = forge::api::core::bytes{};
         auto response = deadline_response(request);
         forge::raw::pack(encoded, response);
         co_await write_transport_frame(stream, encoded, settings.max_frame_size);
      } catch (const forge::exceptions::base& transport_error) {
         if (is_clean_close(transport_error)) {
            stop();
            co_return;
         }
         fail(std::current_exception());
      } catch (...) {
         fail(make_internal_failure("API stream server deadline response failed"));
      }
   }

   boost::asio::awaitable<void> write_responses(const std::shared_ptr<active_call>& call,
                                                const std::vector<forge::api::core::frame>& responses) {
      if (call->cancelled || responses.empty()) {
         co_return;
      }
      auto ticket = co_await writer.acquire();
      for (const auto& response : responses) {
         if (call->cancelled) {
            co_return;
         }
         auto encoded = forge::api::core::bytes{};
         forge::raw::pack(encoded, response);
         co_await write_transport_frame(stream, encoded, settings.max_frame_size);
      }
   }

   boost::asio::awaitable<void> run_call(forge::api::core::frame value, const std::shared_ptr<active_call>& call) {
      const auto id = value.id;
      try {
         co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::enable_total_cancellation{});
         auto responses = co_await dispatcher.dispatch(std::move(value));
         call->deadline.cancel();
         call->dispatching = false;
         co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
         co_await write_responses(call, responses);
         erase_call(id, call);
      } catch (const boost::system::system_error& error) {
         call->dispatching = false;
         erase_call(id, call);
         if (!call->cancelled || error.code() != boost::asio::error::operation_aborted) {
            fail(make_internal_failure("API stream server operation failed"));
         }
      } catch (const forge::asio::exceptions::canceled&) {
         call->dispatching = false;
         erase_call(id, call);
         if (!call->cancelled) {
            fail(make_internal_failure("API stream server operation was cancelled unexpectedly"));
         }
      } catch (const forge::exceptions::base&) {
         call->dispatching = false;
         erase_call(id, call);
         if (!call->cancelled) {
            fail(std::current_exception());
         }
      } catch (...) {
         call->dispatching = false;
         erase_call(id, call);
         if (!call->cancelled) {
            fail(make_internal_failure("API stream server operation failed"));
         }
      }
      finish_call_task();
   }

   boost::asio::awaitable<void> start_call(forge::api::core::frame value) {
      if (active.contains(value.id.value)) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error, "duplicate active API stream call",
                               forge::exceptions::ctx("call_id", value.id.value));
      }
      if (active.size() >= settings.max_inflight) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                               "API stream max inflight calls exceeded",
                               forge::exceptions::ctx("max_inflight", settings.max_inflight));
      }
      auto call = std::make_shared<active_call>(executor);
      const auto id = value.id.value;
      active.emplace(id, call);
      ++outstanding_call_tasks;
      try {
         if (settings.deadline.count() > 0) {
            boost::asio::co_spawn(
                executor,
                [self = shared_from_this(), request = value, call]() mutable -> boost::asio::awaitable<void> {
                   co_await self->watch_deadline(std::move(request), call);
                },
                boost::asio::detached);
         }
         boost::asio::co_spawn(
             executor,
             [self = shared_from_this(), value = std::move(value), call]() mutable -> boost::asio::awaitable<void> {
                co_await self->run_call(std::move(value), call);
             },
             boost::asio::bind_cancellation_slot(call->cancellation->slot(), boost::asio::detached));
      } catch (...) {
         call->deadline.cancel();
         active.erase(id);
         finish_call_task();
         throw;
      }
      co_await boost::asio::post(executor, boost::asio::use_awaitable);
   }

   boost::asio::awaitable<void> run() {
      auto buffer = std::vector<std::uint8_t>{};
      auto consumed = std::size_t{0};
      auto clean_close = false;
      try {
         while (!closed) {
            auto payload = co_await read_transport_frame(stream, buffer, consumed, settings.max_frame_size);
            auto request = decode_wire_frame(std::move(payload), settings.max_frame_size);
            if (request.kind == forge::api::core::frame_kind::cancel) {
               co_await cancel_call(std::move(request));
            } else {
               co_await start_call(std::move(request));
            }
         }
      } catch (const forge::exceptions::base& error) {
         if (!is_clean_close(error)) {
            stop();
            throw;
         }
         clean_close = true;
      } catch (const boost::system::system_error& error) {
         stop();
         if (error.code() != boost::asio::error::operation_aborted) {
            FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::protocol_error,
                                  "API stream transport operation failed",
                                  forge::exceptions::ctx("cause", error.what()));
         }
      } catch (const std::exception& error) {
         stop();
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::protocol_error,
                               "API stream transport operation failed", forge::exceptions::ctx("cause", error.what()));
      } catch (...) {
         stop();
         FORGE_THROW_EXCEPTION(forge::net::transport::exceptions::protocol_error,
                               "API stream transport operation failed");
      }
      if (clean_close && !active.empty()) {
         if (settings.disconnect_grace.count() <= 0) {
            stop();
         } else {
            completion.expires_after(settings.disconnect_grace);
            auto error = boost::system::error_code{};
            co_await completion.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            if (!active.empty()) {
               stop();
            }
         }
      }
      while (outstanding_call_tasks != 0) {
         completion.expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await completion.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      }
      if (failure) {
         std::rethrow_exception(failure);
      }
   }

   forge::net::transport::stream stream;
   forge::api::core::frame_dispatcher dispatcher;
   options settings;
   strand_type executor;
   boost::asio::steady_timer completion;
   forge::asio::gate writer;
   std::unordered_map<std::uint64_t, std::shared_ptr<active_call>> active;
   std::size_t outstanding_call_tasks = 0;
   std::exception_ptr failure;
   bool closed = false;
};

} // namespace

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan,
                                          options value) {
   co_await serve_stream(std::move(stream), std::move(plan), value, {});
}

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan,
                                          options value, forge::api::core::metadata trusted_metadata) {
   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = boost::asio::make_strand(executor);
   auto state =
       std::make_shared<server_state>(std::move(stream), std::move(plan), value, std::move(trusted_metadata), strand);
   co_await boost::asio::co_spawn(
       strand, [state]() -> boost::asio::awaitable<void> { co_await state->run(); }, boost::asio::use_awaitable);
}

} // namespace forge::api::stream
