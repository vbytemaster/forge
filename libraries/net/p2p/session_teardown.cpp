module;

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

module forge.net.p2p.node;

#include "details/session_teardown.hxx"

namespace forge::net::p2p::detail {
namespace {

struct teardown_waiter {
   explicit teardown_waiter(boost::asio::any_io_executor executor)
       : strand{boost::asio::make_strand(std::move(executor))},
         timer{strand, boost::asio::steady_timer::time_point::max()} {}

   boost::asio::strand<boost::asio::any_io_executor> strand;
   boost::asio::steady_timer timer;
};

void wake(const std::shared_ptr<teardown_waiter>& waiter) noexcept {
   boost::asio::dispatch(waiter->strand, [waiter] {
      try {
         waiter->timer.expires_at(boost::asio::steady_timer::time_point::min());
         waiter->timer.cancel();
      } catch (...) {
         // Teardown completion must remain noexcept.
      }
   });
}

} // namespace

struct session_teardown::state : std::enable_shared_from_this<state> {
   explicit state(boost::asio::any_io_executor executor_value) : executor{std::move(executor_value)} {}

   boost::asio::any_io_executor executor;
   mutable std::mutex mutex;
   bool started = false;
   bool completed = false;
   std::size_t pending = 0;
   std::size_t tracked = 0;
   std::exception_ptr failure;
   std::vector<operation> operations;
   std::vector<std::shared_ptr<teardown_waiter>> waiters;

   void complete() noexcept {
      auto ready = std::vector<std::shared_ptr<teardown_waiter>>{};
      {
         const auto lock = std::scoped_lock{mutex};
         if (completed) {
            return;
         }
         completed = true;
         operations.clear();
         ready.swap(waiters);
      }
      for (const auto& waiter : ready) {
         wake(waiter);
      }
   }

   void complete_one(std::exception_ptr error = {}) noexcept {
      auto ready = std::vector<std::shared_ptr<teardown_waiter>>{};
      {
         const auto lock = std::scoped_lock{mutex};
         if (completed) {
            return;
         }
         if (error && !failure) {
            failure = std::move(error);
         }
         if (--pending != 0) {
            return;
         }
         completed = true;
         operations.clear();
         ready.swap(waiters);
      }
      for (const auto& waiter : ready) {
         wake(waiter);
      }
   }

   void release_ticket() noexcept {
      auto complete_after_start = false;
      {
         const auto lock = std::scoped_lock{mutex};
         if (tracked == 0) {
            return;
         }
         --tracked;
         complete_after_start = started;
      }
      if (complete_after_start) {
         complete_one();
      }
   }

   [[nodiscard]] std::exception_ptr error() const {
      const auto lock = std::scoped_lock{mutex};
      return failure;
   }
};

session_teardown::session_teardown(boost::asio::any_io_executor executor)
    : state_{std::make_shared<state>(std::move(executor))} {}

session_teardown::ticket::ticket(std::shared_ptr<state> state) : state_{std::move(state)} {}

session_teardown::ticket::ticket(ticket&& other) noexcept : state_{std::move(other.state_)} {}

session_teardown::ticket& session_teardown::ticket::operator=(ticket&& other) noexcept {
   if (this != &other) {
      release();
      state_ = std::move(other.state_);
   }
   return *this;
}

session_teardown::ticket::~ticket() {
   release();
}

bool session_teardown::ticket::active() const noexcept {
   return static_cast<bool>(state_);
}

void session_teardown::ticket::release() noexcept {
   if (auto state = std::move(state_)) {
      state->release_ticket();
   }
}

session_teardown::ticket session_teardown::track() noexcept {
   const auto lock = std::scoped_lock{state_->mutex};
   if (state_->started) {
      return {};
   }
   ++state_->tracked;
   return ticket{state_};
}

void session_teardown::start(std::vector<operation> operations) noexcept {
   auto operation_count = std::size_t{};
   auto complete_immediately = false;
   {
      const auto lock = std::scoped_lock{state_->mutex};
      if (state_->started) {
         return;
      }
      state_->started = true;
      state_->operations = std::move(operations);
      state_->pending = state_->operations.size() + state_->tracked;
      operation_count = state_->operations.size();
      complete_immediately = state_->pending == 0;
   }

   if (complete_immediately) {
      state_->complete();
      return;
   }

   for (auto index = std::size_t{0}; index < operation_count; ++index) {
      auto state = state_;
      try {
         boost::asio::co_spawn(
             state->executor,
             [state, index]() -> boost::asio::awaitable<void> {
                auto& operation = state->operations[index];
                try {
                   if (operation.close) {
                      co_await operation.close();
                   }
                } catch (...) {
                   try {
                      if (operation.cancel) {
                         operation.cancel();
                      }
                   } catch (...) {
                   }
                }
                state->complete_one();
             },
             boost::asio::detached);
      } catch (...) {
         auto failure = std::current_exception();
         try {
            auto& operation = state->operations[index];
            if (operation.cancel) {
               operation.cancel();
            }
         } catch (...) {
         }
         state->complete_one(std::move(failure));
      }
   }
}

boost::asio::awaitable<void> session_teardown::wait() const {
   co_await boost::asio::this_coro::reset_cancellation_state(boost::asio::disable_cancellation{});
   const auto executor = co_await boost::asio::this_coro::executor;
   auto waiter = std::make_shared<teardown_waiter>(executor);

   auto switch_error = boost::system::error_code{};
   co_await boost::asio::dispatch(waiter->strand,
                                  boost::asio::redirect_error(boost::asio::use_awaitable, switch_error));
   if (switch_error) {
      throw boost::system::system_error{switch_error};
   }

   auto ready = false;
   {
      const auto lock = std::scoped_lock{state_->mutex};
      ready = state_->completed;
      if (!ready) {
         state_->waiters.push_back(waiter);
      }
   }

   if (!ready) {
      auto error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
      static_cast<void>(error);
   }

   if (auto failure = state_->error()) {
      std::rethrow_exception(failure);
   }
}

} // namespace forge::net::p2p::detail
