module;

#include <boost/asio/awaitable.hpp>

#include <utility>

module forge.api.stream.server;

namespace forge::api::stream {

boost::asio::awaitable<void> serve_stream(
   forge::net::transport::stream stream,
   forge::api::core::binding_plan plan, options value) {
   co_await serve_stream(std::move(stream), std::move(plan), std::move(value),
                         {});
}

boost::asio::awaitable<void> serve_stream(
   forge::net::transport::stream stream,
   forge::api::core::binding_plan plan, options value,
   forge::api::core::metadata trusted_metadata) {
   auto live = session{std::move(stream), std::move(plan), std::move(value),
                       std::move(trusted_metadata)};
   co_await live.async_serve();
}

} // namespace forge::api::stream
