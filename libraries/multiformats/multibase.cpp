module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

module forge.multiformats.multibase;

import forge.multiformats.exceptions;

import forge.crypto.base32;
import forge.crypto.base58;

namespace forge::multiformats {

std::string multibase_encode(multibase_code code, std::span<const std::uint8_t> data) {
   switch (code) {
      case multibase_code::base58btc:
         return std::string{"z"} + forge::crypto::base58_encode(data);
      case multibase_code::base32:
         return std::string{"b"} + forge::crypto::base32_encode(data);
      case multibase_code::base32_upper:
         return std::string{"B"} +
                forge::crypto::base32_encode(data, {.alphabet_case = forge::crypto::base32_case::upper});
   }
   throw exceptions::invalid_format{"unsupported multibase code"};
}

decoded_multibase multibase_decode(std::string_view value) {
   if (value.empty()) {
      throw exceptions::invalid_format{"multibase value is missing prefix"};
   }

   const auto payload = value.substr(1);
   switch (value.front()) {
      case 'z':
         return {.code = multibase_code::base58btc, .bytes = forge::crypto::base58_decode(payload)};
      case 'b':
         return {.code = multibase_code::base32, .bytes = forge::crypto::base32_decode(payload)};
      case 'B':
         return {.code = multibase_code::base32_upper, .bytes = forge::crypto::base32_decode(payload)};
      default:
         throw exceptions::invalid_format{"unsupported multibase prefix"};
   }
}

} // namespace forge::multiformats
