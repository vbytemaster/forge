module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

module forge.net.transport.frame;

namespace forge::net::transport {
namespace {

constexpr auto header_size = std::size_t{4};

[[nodiscard]] std::size_t max_frame_bytes(frame_options options) {
   if constexpr (sizeof(std::size_t) <= sizeof(std::uint32_t)) {
      if (options.max_size > static_cast<std::uint32_t>(std::numeric_limits<std::size_t>::max() - header_size)) {
         FORGE_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds addressable size");
      }
   }
   return header_size + static_cast<std::size_t>(options.max_size);
}

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
      FORGE_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
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
      FORGE_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds max_size");
   }

   const auto payload_size = static_cast<std::size_t>(size);
   if (payload_size > std::numeric_limits<std::size_t>::max() - header_size) {
      FORGE_THROW_EXCEPTION(exceptions::frame_too_large, "transport frame payload exceeds addressable size");
   }
   if (payload_size > bytes.size() - header_size) {
      return {.status = frame_decode_status::need_more_data};
   }

   const auto total = header_size + payload_size;
   return {.status = frame_decode_status::complete,
           .payload = {bytes.data() + header_size, payload_size},
           .consumed = total};
}

std::size_t frame_buffer_limit(frame_options options) {
   const auto one_frame = max_frame_bytes(options);
   if (options.max_buffered_size != 0) {
      if (options.max_buffered_size < one_frame) {
         FORGE_THROW_EXCEPTION(exceptions::frame_too_large,
                               "transport max_buffered_size cannot hold one maximum frame");
      }
      return options.max_buffered_size;
   }
   if (one_frame > std::numeric_limits<std::size_t>::max() / 2) {
      return std::numeric_limits<std::size_t>::max();
   }
   return one_frame * 2;
}

frame_decode_result decode_frame(std::span<const std::uint8_t> bytes, frame_options options) {
   const auto decoded = decode_frame_view(bytes, options);
   if (decoded.status == frame_decode_status::need_more_data) {
      return {.status = frame_decode_status::need_more_data};
   }
   auto payload = std::vector<std::uint8_t>{decoded.payload.begin(), decoded.payload.end()};
   return {.status = frame_decode_status::complete, .payload = std::move(payload), .consumed = decoded.consumed};
}

} // namespace forge::net::transport
