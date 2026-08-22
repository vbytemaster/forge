module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.net.yamux.options;

export namespace forge::net::yamux {

enum class side : std::uint8_t {
   initiator,
   responder,
};

struct options {
   std::uint32_t initial_window = 256U * 1024U;
   std::uint32_t max_stream_window = 16 * 1024 * 1024;
   std::uint32_t max_frame_size = 256 * 1024;
   std::size_t max_streams = 4096;
   std::size_t max_pending_accepts = 256;
   std::size_t max_stream_buffer = 1024 * 1024;
   std::size_t max_session_buffer = 16 * 1024 * 1024;
   std::chrono::milliseconds close_timeout{5'000};
   std::chrono::milliseconds write_timeout{5'000};
};

} // namespace forge::net::yamux
