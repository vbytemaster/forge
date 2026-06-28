module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module forge.plugins.db.rocksdb.types;

namespace forge::plugins::db::rocksdb {

std::vector<std::byte> make_key(std::string_view text) {
   return to_bytes(text);
}

std::vector<std::byte> make_u64_key(std::uint64_t value) {
   std::vector<std::byte> key;
   append_u64_be(key, value);
   return key;
}

void append_u8(std::vector<std::byte>& key, std::uint8_t value) {
   key.push_back(static_cast<std::byte>(value));
}

void append_u32_be(std::vector<std::byte>& key, std::uint32_t value) {
   for (int shift = 24; shift >= 0; shift -= 8) {
      key.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
   }
}

void append_u64_be(std::vector<std::byte>& key, std::uint64_t value) {
   for (int shift = 56; shift >= 0; shift -= 8) {
      key.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
   }
}

std::uint64_t read_u64_be(std::span<const std::byte> bytes) {
   if (bytes.size() != 8) {
      throw std::invalid_argument{"RocksDB u64 key component must be exactly 8 bytes"};
   }

   std::uint64_t value = 0;
   for (const auto byte : bytes) {
      value = (value << 8U) | std::to_integer<std::uint8_t>(byte);
   }
   return value;
}

std::vector<std::byte> to_bytes(std::string_view text) {
   std::vector<std::byte> bytes;
   bytes.reserve(text.size());
   for (const auto ch : text) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
   }
   return bytes;
}

std::string to_string(std::span<const std::byte> bytes) {
   std::string text;
   text.reserve(bytes.size());
   for (const auto byte : bytes) {
      text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
   }
   return text;
}

} // namespace forge::plugins::db::rocksdb
