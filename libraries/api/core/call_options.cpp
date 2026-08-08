module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

module forge.api.core.call_options;

#include "details/call_state.hxx"

namespace forge::api::core::detail {

std::shared_ptr<call_operation>
make_call_operation(
   boost::asio::any_io_executor executor,
   std::vector<std::shared_ptr<stream_endpoint>> endpoints,
   std::optional<std::chrono::steady_clock::time_point> deadline) {
   return std::make_shared<call_state>(
      std::move(executor), std::move(endpoints), deadline);
}

} // namespace forge::api::core::detail
