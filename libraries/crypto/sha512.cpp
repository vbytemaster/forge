module;
#include <forge/exceptions/macros.hpp>
#include <cstring>
#include <exception>
#include <memory>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string>

module forge.crypto.sha512;

import forge.core.utility;
import forge.crypto.hex;
import forge.crypto.hmac;
import forge.exceptions;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

#include "_digest_common.hpp"
#include "_evp_digest.hpp"

namespace forge::crypto {

sha512::sha512() {
   memset(_hash, 0, sizeof(_hash));
}
sha512::sha512(const std::string& hex_str) {
   auto bytes_written = forge::crypto::from_hex(hex_str, (char*)_hash, sizeof(_hash));
   if (bytes_written < sizeof(_hash))
      memset((char*)_hash + bytes_written, 0, (sizeof(_hash) - bytes_written));
}

std::string sha512::str() const {
   return forge::crypto::to_hex((char*)_hash, sizeof(_hash));
}
sha512::operator std::string() const {
   return str();
}

char* sha512::data() {
   return (char*)&_hash[0];
}
const char* sha512::data() const {
   return (const char*)&_hash[0];
}

struct sha512::encoder::impl {
   forge::detail::evp_digest_context ctx;
};

sha512::encoder::~encoder() {}
sha512::encoder::encoder() : my(std::make_unique<impl>()) {
   reset();
}

sha512 sha512::hash(const char* d, uint32_t dlen) {
   encoder e;
   e.write(d, dlen);
   return e.result();
}
sha512 sha512::hash(std::span<const std::uint8_t> data) {
   encoder e;
   e.write(data);
   return e.result();
}
sha512 sha512::hash(const std::string& s) {
   return hash(s.c_str(), s.size());
}

void sha512::encoder::write(const char* d, uint32_t dlen) {
   forge::detail::evp_digest_update(my->ctx.get(), d, dlen);
}
void sha512::encoder::write(std::span<const std::uint8_t> data) {
   forge::detail::evp_digest_update(my->ctx.get(), reinterpret_cast<const char*>(data.data()),
                                  static_cast<std::uint32_t>(data.size()));
}
sha512 sha512::encoder::result() {
   sha512 h;
   forge::detail::evp_digest_final(my->ctx.get(), h.data(), h.data_size());
   return h;
}
void sha512::encoder::reset() {
   forge::detail::evp_digest_init(my->ctx.get(), EVP_sha512());
}

sha512 operator<<(const sha512& h1, uint32_t i) {
   sha512 result;
   forge::detail::shift_l(h1.data(), result.data(), result.data_size(), i);
   return result;
}
sha512 operator^(const sha512& h1, const sha512& h2) {
   sha512 result;
   result._hash[0] = h1._hash[0] ^ h2._hash[0];
   result._hash[1] = h1._hash[1] ^ h2._hash[1];
   result._hash[2] = h1._hash[2] ^ h2._hash[2];
   result._hash[3] = h1._hash[3] ^ h2._hash[3];
   result._hash[4] = h1._hash[4] ^ h2._hash[4];
   result._hash[5] = h1._hash[5] ^ h2._hash[5];
   result._hash[6] = h1._hash[6] ^ h2._hash[6];
   result._hash[7] = h1._hash[7] ^ h2._hash[7];
   return result;
}
bool operator>=(const sha512& h1, const sha512& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) >= 0;
}
bool operator>(const sha512& h1, const sha512& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) > 0;
}
bool operator<(const sha512& h1, const sha512& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) < 0;
}
bool operator!=(const sha512& h1, const sha512& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) != 0;
}
bool operator==(const sha512& h1, const sha512& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) == 0;
}

void to_variant(const sha512& bi, variant& v) {
   v = std::vector<char>((const char*)&bi, ((const char*)&bi) + sizeof(bi));
}
void from_variant(const variant& v, sha512& bi) {
   std::vector<char> ve = v.as<std::vector<char>>();
   if (ve.size()) {
      memcpy(bi.data(), ve.data(), forge::min<size_t>(ve.size(), sizeof(bi)));
   } else
      memset(bi.data(), char(0), sizeof(bi));
}

template <> unsigned int hmac<sha512>::internal_block_size() const {
   return 128;
}
} // namespace forge::crypto
