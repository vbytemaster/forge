module;
#include <cstdint>
#include <forge/exceptions/macros.hpp>
#include <string>
#include <vector>

export module forge.crypto.hex;

import forge.core.utility;
export import forge.exceptions;
export import forge.crypto.types;

export namespace forge::crypto::hex::exceptions {

enum class code : std::uint16_t {
   invalid_character = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.hex")

using invalid_character = forge::exceptions::coded_exception<code, code::invalid_character>;

} // namespace forge::crypto::hex::exceptions

export namespace forge::crypto {
uint8_t from_hex(char c);
std::string to_hex(const char* d, uint32_t s);
std::string to_hex(const std::uint8_t* d, uint32_t s);
std::string to_hex(const std::vector<char>& data);
std::string to_hex(const bytes& data);

/**
 *  @return the number of bytes decoded
 */
size_t from_hex(const std::string& hex_str, char* out_data, size_t out_data_len);
size_t from_hex(const std::string& hex_str, std::uint8_t* out_data, size_t out_data_len);

/**
 *  @return the hex string of `n`
 */
template <typename I> std::string itoh(I n, size_t hlen = sizeof(I) << 1) {
   static const char* digits = "0123456789abcdef";
   std::string r(hlen, '0');
   for (size_t i = 0, j = (hlen - 1) * 4; i < hlen; ++i, j -= 4)
      r[i] = digits[(n >> j) & 0x0f];
   return r;
}
} // namespace forge::crypto
