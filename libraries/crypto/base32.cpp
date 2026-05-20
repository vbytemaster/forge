module;

#include <array>
#include <cctype>
#include <span>
#include <string>
#include <string_view>

module fcl.crypto.base32;

namespace fcl::crypto {
namespace {

constexpr auto lower_alphabet = std::string_view{"abcdefghijklmnopqrstuvwxyz234567"};
constexpr auto upper_alphabet = std::string_view{"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"};

[[nodiscard]] std::int32_t decode_value(char ch) noexcept {
   const auto uch = static_cast<unsigned char>(ch);
   const auto lower = static_cast<char>(std::tolower(uch));
   if (lower >= 'a' && lower <= 'z') {
      return lower - 'a';
   }
   if (ch >= '2' && ch <= '7') {
      return 26 + (ch - '2');
   }
   return -1;
}

} // namespace

std::string base32_encode(std::span<const std::uint8_t> data, base32_options options) {
   const auto alphabet = options.alphabet_case == base32_case::upper ? upper_alphabet : lower_alphabet;
   auto output = std::string{};
   output.reserve(((data.size() + 4) / 5) * 8);

   std::uint32_t buffer = 0;
   int bits = 0;
   for (auto byte : data) {
      buffer = (buffer << 8U) | byte;
      bits += 8;
      while (bits >= 5) {
         bits -= 5;
         output.push_back(alphabet[(buffer >> bits) & 0x1fU]);
      }
   }

   if (bits > 0) {
      output.push_back(alphabet[(buffer << (5 - bits)) & 0x1fU]);
   }

   if (options.padding) {
      while ((output.size() % 8) != 0) {
         output.push_back('=');
      }
   }

   return output;
}

bytes base32_decode(std::string_view value) {
   auto payload = value;
   while (!payload.empty() && payload.back() == '=') {
      payload.remove_suffix(1);
   }

   auto output = bytes{};
   output.reserve((payload.size() * 5) / 8);

   std::uint32_t buffer = 0;
   int bits = 0;
   for (auto ch : payload) {
      const auto decoded = decode_value(ch);
      if (decoded < 0) {
         throw error{error_kind::invalid_options, "base32 input contains an invalid character"};
      }

      buffer = (buffer << 5U) | static_cast<std::uint32_t>(decoded);
      bits += 5;
      if (bits >= 8) {
         bits -= 8;
         output.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xffU));
      }
   }

   if (bits > 0 && ((buffer << (8 - bits)) & 0xffU) != 0) {
      throw error{error_kind::invalid_options, "base32 input has non-zero trailing bits"};
   }

   return output;
}

} // namespace fcl::crypto
