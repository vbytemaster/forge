module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module fcl.transport.frame;

import fcl.transport.exceptions;

export namespace fcl::transport {

struct frame_options {
   std::uint32_t max_size = 16 * 1024 * 1024;
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

} // namespace fcl::transport
