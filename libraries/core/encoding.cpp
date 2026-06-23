module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module forge.core.encoding;

namespace forge::encoding {
namespace {

constexpr char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_value(char value) noexcept {
   if (value >= 'A' && value <= 'Z') {
      return value - 'A';
   }
   if (value >= 'a' && value <= 'z') {
      return value - 'a' + 26;
   }
   if (value >= '0' && value <= '9') {
      return value - '0' + 52;
   }
   if (value == '+') {
      return 62;
   }
   if (value == '/') {
      return 63;
   }
   if (value == '=') {
      return -2;
   }
   return -1;
}

std::size_t base64_data_size(std::string_view input) {
   auto padding = std::size_t{0};
   while (padding < input.size() && input[input.size() - padding - 1U] == '=') {
      ++padding;
   }
   if (padding > 2U) {
      throw std::invalid_argument{"base64 padding is invalid"};
   }

   const auto data_size = input.size() - padding;
   if (padding > 0U && input.size() % 4U != 0U) {
      throw std::invalid_argument{"base64 padding is invalid"};
   }

   const auto remainder = data_size % 4U;
   if (remainder == 1U) {
      throw std::invalid_argument{"base64 length is invalid"};
   }
   if (padding == 1U && remainder != 3U) {
      throw std::invalid_argument{"base64 padding is invalid"};
   }
   if (padding == 2U && remainder != 2U) {
      throw std::invalid_argument{"base64 padding is invalid"};
   }

   for (auto index = std::size_t{0}; index < input.size(); ++index) {
      const auto decoded = base64_value(input[index]);
      if (decoded < 0 && decoded != -2) {
         throw std::invalid_argument{"encountered non-base64 character"};
      }
      if (decoded == -2 && index < data_size) {
         throw std::invalid_argument{"base64 padding is invalid"};
      }
   }

   if (remainder == 2U && data_size > 0U && (base64_value(input[data_size - 1U]) & 0x0f) != 0) {
      throw std::invalid_argument{"base64 trailing bits are non-canonical"};
   }
   if (remainder == 3U && data_size > 0U && (base64_value(input[data_size - 1U]) & 0x03) != 0) {
      throw std::invalid_argument{"base64 trailing bits are non-canonical"};
   }

   return data_size;
}

char hex_char(unsigned value) noexcept {
   return static_cast<char>(value < 10 ? ('0' + value) : ('a' + value - 10));
}

unsigned from_hex_char(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<unsigned>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<unsigned>(value - 'a' + 10);
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<unsigned>(value - 'A' + 10);
   }
   throw std::invalid_argument{"invalid hex character"};
}

} // namespace

std::string to_base64(std::span<const std::uint8_t> data) {
   auto out = std::string{};
   out.reserve(((data.size() + 2U) / 3U) * 4U);
   for (auto i = std::size_t{0}; i < data.size(); i += 3U) {
      const auto b0 = data[i];
      const auto b1 = (i + 1U < data.size()) ? data[i + 1U] : 0U;
      const auto b2 = (i + 2U < data.size()) ? data[i + 2U] : 0U;
      out.push_back(base64_chars[b0 >> 2U]);
      out.push_back(base64_chars[((b0 & 0x03U) << 4U) | (b1 >> 4U)]);
      out.push_back((i + 1U < data.size()) ? base64_chars[((b1 & 0x0fU) << 2U) | (b2 >> 6U)] : '=');
      out.push_back((i + 2U < data.size()) ? base64_chars[b2 & 0x3fU] : '=');
   }
   return out;
}

std::vector<std::uint8_t> from_base64(std::string_view input) {
   const auto data_size = base64_data_size(input);
   auto out = std::vector<std::uint8_t>{};
   out.reserve((data_size * 3U) / 4U);
   auto value = std::uint32_t{0};
   auto bits = -8;
   for (auto index = std::size_t{0}; index < data_size; ++index) {
      const auto ch = input[index];
      const auto decoded = base64_value(ch);
      value = (value << 6U) | static_cast<std::uint32_t>(decoded);
      bits += 6;
      if (bits >= 0) {
         out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xff));
         bits -= 8;
         value &= 0x00ffffffU;
      }
   }
   return out;
}

std::string to_hex(std::span<const std::uint8_t> data) {
   auto out = std::string{};
   out.reserve(data.size() * 2U);
   for (const auto byte : data) {
      out.push_back(hex_char(byte >> 4U));
      out.push_back(hex_char(byte & 0x0fU));
   }
   return out;
}

std::size_t from_hex(std::string_view input, std::span<std::uint8_t> output) {
   if (input.size() % 2U != 0U) {
      throw std::invalid_argument{"hex input length must be even"};
   }
   const auto count = input.size() / std::size_t{2};
   if (count > output.size()) {
      throw std::out_of_range{"hex output buffer too small"};
   }
   for (auto i = std::size_t{0}; i < count; ++i) {
      output[i] = static_cast<std::uint8_t>((from_hex_char(input[2U * i]) << 4U) | from_hex_char(input[2U * i + 1U]));
   }
   return count;
}

} // namespace forge::encoding
