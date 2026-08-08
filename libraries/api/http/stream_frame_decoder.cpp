module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

module forge.api.http.binding;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.net.http.body;
import forge.net.transport.frame;
import forge.raw.raw;

#include "details/stream_frame_decoder.hxx"

namespace forge::api::http::detail {
namespace {

constexpr auto http_stream_wire_major = std::uint16_t{2};
constexpr auto length_prefix_bytes = std::size_t{4};

[[nodiscard]] forge::raw::unpack_limits decode_limits(std::size_t size) {
   const auto bounded = static_cast<std::uint32_t>(
      std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
   return forge::raw::unpack_limits{
      .max_container_elements = bounded,
      .max_total_container_elements = bounded,
      .max_bytes = bounded,
      .first_container_elements = bounded,
   };
}

} // namespace

stream_frame_decoder::stream_frame_decoder(forge::net::http::body_reader body,
                                           std::uint32_t max_frame_bytes)
    : body_{std::move(body)}, max_frame_bytes_{max_frame_bytes} {
   if (!body_.valid() || max_frame_bytes_ == 0) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "HTTP API stream decoder is not configured");
   }
}

boost::asio::awaitable<std::optional<forge::api::core::frame>>
stream_frame_decoder::async_read() {
   for (;;) {
      const auto available = std::span<const std::uint8_t>{buffered_}.subspan(offset_);
      const auto decoded = forge::net::transport::decode_frame_view(
         available, forge::net::transport::frame_options{.max_size = max_frame_bytes_});
      if (decoded.status == forge::net::transport::frame_decode_status::complete) {
         offset_ += decoded.consumed;
         auto [version, value] = forge::raw::unpack_exact<
            std::tuple<std::uint16_t, forge::api::core::frame>>(
               decoded.payload, decode_limits(decoded.payload.size()));
         if (version != http_stream_wire_major) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::incompatible_version,
                                  "HTTP API stream wire major is incompatible",
                                  forge::exceptions::ctx("wire_major", version));
         }
         compact();
         co_return std::move(value);
      }

      if (eof_) {
         if (!available.empty()) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                                  "HTTP API stream ended with a truncated frame");
         }
         co_return std::nullopt;
      }

      auto chunk = co_await body_.async_read();
      if (!chunk) {
         eof_ = true;
         continue;
      }
      compact();
      if (chunk->bytes.size() > std::numeric_limits<std::size_t>::max() - buffered_.size()) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                               "HTTP API stream decoder buffer overflowed");
      }
      const auto old_size = buffered_.size();
      buffered_.resize(old_size + chunk->bytes.size());
      if (!chunk->bytes.empty()) {
         std::memcpy(buffered_.data() + old_size, chunk->bytes.data(), chunk->bytes.size());
      }
      if (buffered_.size() > static_cast<std::size_t>(max_frame_bytes_) + length_prefix_bytes) {
         const auto probe = forge::net::transport::decode_frame_view(
            buffered_, forge::net::transport::frame_options{.max_size = max_frame_bytes_});
         if (probe.status != forge::net::transport::frame_decode_status::complete) {
            FORGE_THROW_EXCEPTION(forge::api::core::exceptions::resource_exhausted,
                                  "HTTP API stream frame exceeds the configured limit");
         }
      }
   }
}

void stream_frame_decoder::cancel() noexcept {
   body_.cancel();
}

forge::net::http::body_chunk
stream_frame_decoder::encode(const forge::api::core::frame& value,
                             std::uint32_t max_frame_bytes) {
   auto payload = forge::raw::pack(std::tuple{http_stream_wire_major, value});
   auto encoded = forge::net::transport::encode_frame(
      payload, forge::net::transport::frame_options{.max_size = max_frame_bytes});
   auto bytes = std::vector<std::byte>(encoded.size());
   if (!encoded.empty()) {
      std::memcpy(bytes.data(), encoded.data(), encoded.size());
   }
   return forge::net::http::body_chunk{.bytes = std::move(bytes)};
}

void stream_frame_decoder::compact() {
   if (offset_ == 0) {
      return;
   }
   if (offset_ == buffered_.size()) {
      buffered_.clear();
      offset_ = 0;
      return;
   }
   buffered_.erase(buffered_.begin(), buffered_.begin() + static_cast<std::ptrdiff_t>(offset_));
   offset_ = 0;
}

} // namespace forge::api::http::detail
