module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

module forge.api.websocket.stream;

#include "details/websocket_stream.hxx"

namespace forge::api::websocket {

forge::net::transport::stream
as_transport_stream(forge::net::websocket::connection::ptr connection,
                    std::uint32_t max_frame_size,
                    std::uint64_t max_buffered_bytes) {
   auto model = std::make_shared<detail::websocket_stream>(
      std::move(connection), max_frame_size, max_buffered_bytes);
   model->install_handlers();
   return forge::net::transport::detail::stream_access::make(std::move(model));
}

} // namespace forge::api::websocket
