module;

#include <cstddef>
#include <cstdint>

export module forge.api.websocket.stream;

export import forge.net.transport.stream;
export import forge.net.websocket.connection;

export namespace forge::api::websocket {

[[nodiscard]] forge::net::transport::stream
as_transport_stream(forge::net::websocket::connection::ptr connection,
                    std::uint32_t max_frame_size = 2U * 1024U * 1024U,
                    std::uint64_t max_buffered_bytes = 16U * 1024U * 1024U);

} // namespace forge::api::websocket
