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
   if (payload.size() > options.max_size) {
      FCL_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
   }

   auto out = std::vector<std::uint8_t>{};
   out.reserve(header_size + payload.size());
   write_u32_be(out, static_cast<std::uint32_t>(payload.size()));
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

frame_decode_result decode_frame(std::span<const std::uint8_t> bytes, frame_options options) {
   if (bytes.size() < header_size) {
      return {.status = frame_decode_status::need_more_data};
   }

   const auto size = read_u32_be(std::span<const std::uint8_t, header_size>{bytes.data(), header_size});
   if (size > options.max_size) {
      FCL_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
   }

   const auto total = header_size + static_cast<std::size_t>(size);
   if (bytes.size() < total) {
      return {.status = frame_decode_status::need_more_data};
   }

   auto payload = std::vector<std::uint8_t>{bytes.begin() + static_cast<std::ptrdiff_t>(header_size),
                                            bytes.begin() + static_cast<std::ptrdiff_t>(total)};
   return {.status = frame_decode_status::complete, .payload = std::move(payload), .consumed = total};
}

} // namespace fcl::transport
