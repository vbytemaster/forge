module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

module forge.net.transport.stream;

import forge.net.transport.exceptions;

#include "details/bounded_frame_buffer.hxx"

namespace forge::net::transport::detail {
namespace {

[[noreturn]] void throw_frame_buffer_limit() {
   FORGE_THROW_EXCEPTION(exceptions::frame_too_large, "transport framed input exceeds max_buffered_size");
}

[[noreturn]] void throw_invalid_frame_buffer() {
   FORGE_THROW_EXCEPTION(exceptions::invalid_buffer, "transport framed input is internally inconsistent");
}

} // namespace

bool bounded_frame_buffer::empty() const noexcept {
   return bytes_.empty();
}

std::size_t bounded_frame_buffer::size() const noexcept {
   return bytes_.size();
}

std::span<const std::uint8_t> bounded_frame_buffer::bytes() const noexcept {
   return bytes_;
}

void bounded_frame_buffer::append(std::span<const std::uint8_t> bytes, frame_options options) {
   const auto limit = frame_buffer_limit(options);
   if (bytes_.size() > limit || bytes.size() > limit - bytes_.size()) {
      throw_frame_buffer_limit();
   }
   if (bytes.empty()) {
      return;
   }

   const auto total = bytes_.size() + bytes.size();
   auto merged = std::vector<std::uint8_t>{};
   merged.reserve(total);
   merged.insert(merged.end(), bytes_.begin(), bytes_.end());
   merged.insert(merged.end(), bytes.begin(), bytes.end());
   bytes_ = std::move(merged);
}

void bounded_frame_buffer::append_prefetched(std::vector<std::uint8_t> bytes) {
   if (bytes.empty()) {
      return;
   }
   if (bytes_.empty()) {
      bytes_ = std::move(bytes);
      return;
   }
   if (bytes.size() > std::numeric_limits<std::size_t>::max() - bytes_.size()) {
      throw_invalid_frame_buffer();
   }
   const auto total = bytes_.size() + bytes.size();
   auto merged = std::vector<std::uint8_t>{};
   merged.reserve(total);
   merged.insert(merged.end(), bytes_.begin(), bytes_.end());
   merged.insert(merged.end(), bytes.begin(), bytes.end());
   bytes_ = std::move(merged);
}

void bounded_frame_buffer::enforce_limit(frame_options options) const {
   if (bytes_.size() > frame_buffer_limit(options)) {
      throw_frame_buffer_limit();
   }
}

chunk bounded_frame_buffer::take_all() {
   auto out = chunk{std::move(bytes_)};
   bytes_.clear();
   return out;
}

chunk bounded_frame_buffer::take_frame_payload(std::size_t frame_size, std::size_t payload_size) {
   if (frame_size > bytes_.size() || payload_size > frame_size) {
      throw_invalid_frame_buffer();
   }
   const auto payload_offset = frame_size - payload_size;
   if (frame_size == bytes_.size()) {
      auto out = chunk{std::move(bytes_), payload_offset, payload_size};
      bytes_.clear();
      return out;
   }

   auto out = chunk{std::span<const std::uint8_t>{bytes_.data() + payload_offset, payload_size}};
   auto tail = std::vector<std::uint8_t>{bytes_.begin() + static_cast<std::ptrdiff_t>(frame_size), bytes_.end()};
   bytes_ = std::move(tail);
   return out;
}

} // namespace forge::net::transport::detail
