module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

module forge.objectdb.key;

import forge.exceptions;
import forge.objectdb.exceptions;
import forge.objectdb.types;

namespace forge::objectdb {

namespace {

void append_byte(byte_vector& out, std::uint8_t value) {
   out.push_back(static_cast<std::byte>(value));
}

void append_u32_be(byte_vector& out, std::uint32_t value) {
   append_byte(out, static_cast<std::uint8_t>((value >> 24U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 16U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
   append_byte(out, static_cast<std::uint8_t>(value & 0xffU));
}

void append_u64_be(byte_vector& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      append_byte(out, static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

} // namespace

key_builder::key_builder(key_domain domain) {
   append_u8(static_cast<std::uint8_t>(domain));
}

key_builder& key_builder::append_u8(std::uint8_t value) {
   append_byte(_bytes, value);
   return *this;
}

key_builder& key_builder::append_u32(std::uint32_t value) {
   append_u32_be(_bytes, value);
   return *this;
}

key_builder& key_builder::append_u64(std::uint64_t value) {
   append_u64_be(_bytes, value);
   return *this;
}

key_builder& key_builder::append_i64(std::int64_t value) {
   const auto encoded = static_cast<std::uint64_t>(value) ^ (std::uint64_t{1} << 63U);
   return append_u64(encoded);
}

key_builder& key_builder::append_ordered_bytes(std::span<const std::byte> value) {
   for (const auto byte : value) {
      const auto current = static_cast<std::uint8_t>(byte);
      if (current == 0U) {
         append_byte(_bytes, 0x00U);
         append_byte(_bytes, 0xffU);
      } else {
         append_byte(_bytes, current);
      }
   }
   append_byte(_bytes, 0x00U);
   append_byte(_bytes, 0x00U);
   return *this;
}

key_builder& key_builder::append_ordered_text(std::string_view value) {
   return append_ordered_bytes({reinterpret_cast<const std::byte*>(value.data()), value.size()});
}

key_builder& key_builder::append_raw(std::span<const std::byte> value) {
   _bytes.insert(_bytes.end(), value.begin(), value.end());
   return *this;
}

const byte_vector& key_builder::bytes() const noexcept {
   return _bytes;
}

byte_vector key_builder::finish() {
   return std::move(_bytes);
}

byte_vector make_object_key(table_id table, object_id object) {
   return key_builder{key_domain::object}.append_u32(table.value).append_u64(object.value).finish();
}

byte_vector make_object_prefix(table_id table) {
   return key_builder{key_domain::object}.append_u32(table.value).finish();
}

byte_vector make_index_prefix(table_id table, index_id index) {
   return key_builder{key_domain::index}.append_u32(table.value).append_u32(index.value).finish();
}

byte_vector make_secondary_index_key(table_id table, index_id index, std::span<const std::byte> encoded_value,
                                     object_id object) {
   return key_builder{key_domain::index}
      .append_u32(table.value)
      .append_u32(index.value)
      .append_raw(encoded_value)
      .append_u64(object.value)
      .finish();
}

byte_vector make_sequence_key(table_id table) {
   return key_builder{key_domain::sequence}.append_u32(table.value).finish();
}

key_range prefix_range(std::span<const std::byte> prefix) {
   auto begin = byte_vector{prefix.begin(), prefix.end()};
   auto end = begin;
   for (auto it = end.rbegin(); it != end.rend(); ++it) {
      const auto current = static_cast<std::uint8_t>(*it);
      if (current != std::numeric_limits<std::uint8_t>::max()) {
         *it = static_cast<std::byte>(current + 1U);
         end.resize(static_cast<std::size_t>(std::distance(it, end.rend())));
         return key_range{.begin = std::move(begin), .end = std::move(end), .has_end = true};
      }
   }
   return key_range{.begin = std::move(begin), .end = {}, .has_end = false};
}

bool starts_with(std::span<const std::byte> value, std::span<const std::byte> prefix) noexcept {
   return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::uint64_t read_u64(std::span<const std::byte> value) {
   if (value.size() != 8U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "objectdb u64 key fragment must be exactly 8 bytes",
                            forge::exceptions::ctx("size", value.size()));
   }

   auto result = std::uint64_t{};
   for (const auto byte : value) {
      result = (result << 8U) | static_cast<std::uint64_t>(byte);
   }
   return result;
}

} // namespace forge::objectdb
