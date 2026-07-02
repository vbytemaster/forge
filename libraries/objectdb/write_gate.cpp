module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

module forge.objectdb.store;

#include "details/write_gate.hxx"

namespace forge::objectdb::detail {

struct write_gate::waiter {
   explicit waiter(boost::asio::any_io_executor executor)
       : timer{std::move(executor), boost::asio::steady_timer::time_point::max()} {}

   boost::asio::steady_timer timer;
   bool granted = false;
};

write_gate::ticket::ticket(std::shared_ptr<write_gate> gate) : gate_{std::move(gate)} {}

write_gate::ticket::ticket(ticket&& other) noexcept : gate_{std::move(other.gate_)} {}

write_gate::ticket& write_gate::ticket::operator=(ticket&& other) noexcept {
   if (this != &other) {
      release();
      gate_ = std::move(other.gate_);
   }
   return *this;
}

write_gate::ticket::~ticket() {
   release();
}

void write_gate::ticket::release() noexcept {
   auto gate = std::move(gate_);
   if (gate) {
      gate->release_one();
   }
}

boost::asio::awaitable<write_gate::ticket> write_gate::acquire() {
   const auto executor = co_await boost::asio::this_coro::executor;
   const auto self = shared_from_this();

   for (;;) {
      auto waiter = std::shared_ptr<write_gate::waiter>{};
      {
         auto lock = std::scoped_lock{mutex_};
         if (!held_) {
            held_ = true;
            co_return ticket{self};
         }
         waiter = std::make_shared<write_gate::waiter>(executor);
         waiters_.push_back(waiter);
      }

      auto error = boost::system::error_code{};
      co_await waiter->timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));

      {
         auto lock = std::scoped_lock{mutex_};
         if (waiter->granted) {
            co_return ticket{self};
         }

         const auto found = std::ranges::find(waiters_, waiter);
         if (found != waiters_.end()) {
            waiters_.erase(found);
         }
      }

      if (error) {
         throw boost::system::system_error{error};
      }
   }
}

void write_gate::release_one() noexcept {
   auto waiter = std::shared_ptr<write_gate::waiter>{};
   {
      auto lock = std::scoped_lock{mutex_};
      while (!waiters_.empty() && !waiter) {
         waiter = std::move(waiters_.front());
         waiters_.pop_front();
      }
      if (waiter) {
         waiter->granted = true;
      } else {
         held_ = false;
      }
   }
   if (waiter) {
      waiter->timer.cancel();
   }
}

} // namespace forge::objectdb::detail
