module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

module fcl.multiformats.multibase;

import fcl.crypto.base32;
import fcl.crypto.base58;

namespace fcl::multiformats {

std::string multibase_encode(multibase_code code, std::span<const std::uint8_t> data) {
   switch (code) {
      case multibase_code::base58btc:
         return std::string{"z"} + fcl::crypto::base58_encode(data);
      case multibase_code::base32:
         return std::string{"b"} + fcl::crypto::base32_encode(data);
      case multibase_code::base32_upper:
         return std::string{"B"} +
                fcl::crypto::base32_encode(data, {.alphabet_case = fcl::crypto::base32_case::upper});
   }
   throw format_error{"unsupported multibase code"};
}

decoded_multibase multibase_decode(std::string_view value) {
   if (value.empty()) {
      throw format_error{"multibase value is missing prefix"};
   }

   const auto payload = value.substr(1);
   switch (value.front()) {
      case 'z':
         return {.code = multibase_code::base58btc, .bytes = fcl::crypto::base58_decode(payload)};
      case 'b':
         return {.code = multibase_code::base32, .bytes = fcl::crypto::base32_decode(payload)};
      case 'B':
         return {.code = multibase_code::base32_upper, .bytes = fcl::crypto::base32_decode(payload)};
      default:
         throw format_error{"unsupported multibase prefix"};
   }
}

} // namespace fcl::multiformats
