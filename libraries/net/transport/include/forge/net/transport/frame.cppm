module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module forge.net.transport.frame;

import forge.net.transport.exceptions;

export namespace forge::net::transport {

struct frame_options {
   std::uint32_t max_size = 16 * 1024 * 1024;
   // Zero retains at most two max-sized frames, including each four-byte header.
   std::size_t max_buffered_size = 0;
};

enum class frame_decode_status {
   complete,
   need_more_data,
};

struct frame_decode_result {
   frame_decode_status status = frame_decode_status::need_more_data;
   std::vector<std::uint8_t> payload;
   std::size_t consumed = 0;
};

struct frame_view_decode_result {
   frame_decode_status status = frame_decode_status::need_more_data;
   std::span<const std::uint8_t> payload;
   std::size_t consumed = 0;
};

[[nodiscard]] std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload,
                                                     frame_options options = {});
void encode_frame_to(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> payload,
                     frame_options options = {});
[[nodiscard]] frame_decode_result decode_frame(std::span<const std::uint8_t> bytes, frame_options options = {});
[[nodiscard]] frame_view_decode_result decode_frame_view(std::span<const std::uint8_t> bytes,
                                                         frame_options options = {});
[[nodiscard]] std::size_t frame_buffer_limit(frame_options options = {});

} // namespace forge::net::transport
