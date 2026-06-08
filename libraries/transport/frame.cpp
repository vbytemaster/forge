module;

#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

module fcl.transport.frame;

namespace fcl::transport {
namespace {

constexpr auto header_size = std::size_t{4};

[[nodiscard]] std::uint32_t read_u32_be(std::span<const std::uint8_t, header_size> bytes) noexcept {
   return (static_cast<std::uint32_t>(bytes[0]) << 24U) | (static_cast<std::uint32_t>(bytes[1]) << 16U) |
          (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

void write_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
   out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

} // namespace

std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload, frame_options options) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(header_size + payload.size());
   encode_frame_to(out, payload, options);
   return out;
}

void encode_frame_to(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> payload, frame_options options) {
   if (payload.size() > options.max_size) {
      FCL_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
   }
   write_u32_be(out, static_cast<std::uint32_t>(payload.size()));
   out.insert(out.end(), payload.begin(), payload.end());
}

frame_view_decode_result decode_frame_view(std::span<const std::uint8_t> bytes, frame_options options) {
   if (bytes.size() < header_size) {
      return {.status = frame_decode_status::need_more_data};
   }

   const auto size = read_u32_be(std::span<const std::uint8_t, header_size>{bytes.data(), header_size});
   if (size > options.max_size) {
      FCL_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
   }

   if (static_cast<std::size_t>(size) > bytes.size() - header_size) {
      return {.status = frame_decode_status::need_more_data};
   }

   const auto total = header_size + static_cast<std::size_t>(size);
   return {.status = frame_decode_status::complete,
           .payload = {bytes.data() + header_size, static_cast<std::size_t>(size)},
           .consumed = total};
}

frame_decode_result decode_frame(std::span<const std::uint8_t> bytes, frame_options options) {
   const auto decoded = decode_frame_view(bytes, options);
   if (decoded.status == frame_decode_status::need_more_data) {
      return {.status = frame_decode_status::need_more_data};
   }
   auto payload = std::vector<std::uint8_t>{decoded.payload.begin(), decoded.payload.end()};
   return {.status = frame_decode_status::complete, .payload = std::move(payload), .consumed = decoded.consumed};
}

} // namespace fcl::transport
