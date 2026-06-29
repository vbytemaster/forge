module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module forge.rocksdb.types;

namespace forge::rocksdb {

std::vector<std::byte> make_key(std::string_view text) {
   return to_bytes(text);
}

std::vector<std::byte> make_u64_key(std::uint64_t value) {
   auto key = std::vector<std::byte>{};
   append_u64_be(key, value);
   return key;
}

void append_u8(std::vector<std::byte>& key, std::uint8_t value) {
   key.push_back(static_cast<std::byte>(value));
}

void append_u32_be(std::vector<std::byte>& key, std::uint32_t value) {
   key.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
   key.push_back(static_cast<std::byte>(value & 0xffU));
}

void append_u64_be(std::vector<std::byte>& key, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      key.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xffU));
   }
}

std::uint64_t read_u64_be(std::span<const std::byte> bytes) {
   if (bytes.size() != 8U) {
      throw std::invalid_argument{"u64 key fragment must be exactly 8 bytes"};
   }
   auto value = std::uint64_t{};
   for (auto byte : bytes) {
      value = (value << 8U) | static_cast<std::uint64_t>(byte);
   }
   return value;
}

std::vector<std::byte> to_bytes(std::string_view text) {
   auto bytes = std::vector<std::byte>{};
   bytes.resize(text.size());
   std::memcpy(bytes.data(), text.data(), text.size());
   return bytes;
}

std::string to_string(std::span<const std::byte> bytes) {
   auto text = std::string{};
   text.resize(bytes.size());
   std::memcpy(text.data(), bytes.data(), bytes.size());
   return text;
}

} // namespace forge::rocksdb
