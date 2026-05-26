module;
#include <fcl/exception/macros.hpp>
#include <cstring>
#include <exception>
#include <memory>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string>

module fcl.crypto.sha224;

import fcl.core.utility;
import fcl.crypto.hex;
import fcl.crypto.hmac;
import fcl.exception.exception;
import fcl.variant;

#include "_digest_common.hpp"
#include "_evp_digest.hpp"

namespace fcl::crypto {

sha224::sha224() {
   memset(_hash, 0, sizeof(_hash));
}
sha224::sha224(const std::string& hex_str) {
   auto bytes_written = fcl::crypto::from_hex(hex_str, (char*)_hash, sizeof(_hash));
   if (bytes_written < sizeof(_hash))
      memset((char*)_hash + bytes_written, 0, (sizeof(_hash) - bytes_written));
}

std::string sha224::str() const {
   return fcl::crypto::to_hex((char*)_hash, sizeof(_hash));
}
sha224::operator std::string() const {
   return str();
}

char* sha224::data() {
   return (char*)&_hash[0];
}
const char* sha224::data() const {
   return (const char*)&_hash[0];
}

struct sha224::encoder::impl {
   fcl::detail::evp_digest_context ctx;
};

sha224::encoder::~encoder() {}
sha224::encoder::encoder() : my(std::make_unique<impl>()) {
   reset();
}

sha224 sha224::hash(const char* d, uint32_t dlen) {
   encoder e;
   e.write(d, dlen);
   return e.result();
}
sha224 sha224::hash(const std::string& s) {
   return hash(s.c_str(), s.size());
}

void sha224::encoder::write(const char* d, uint32_t dlen) {
   fcl::detail::evp_digest_update(my->ctx.get(), d, dlen);
}
sha224 sha224::encoder::result() {
   sha224 h;
   fcl::detail::evp_digest_final(my->ctx.get(), h.data(), h.data_size());
   return h;
}
void sha224::encoder::reset() {
   fcl::detail::evp_digest_init(my->ctx.get(), EVP_sha224());
}

sha224 operator<<(const sha224& h1, uint32_t i) {
   sha224 result;
   fcl::detail::shift_l(h1.data(), result.data(), result.data_size(), i);
   return result;
}
sha224 operator^(const sha224& h1, const sha224& h2) {
   sha224 result;
   for (uint32_t i = 0; i < 7; ++i)
      result._hash[i] = h1._hash[i] ^ h2._hash[i];
   return result;
}
bool operator>=(const sha224& h1, const sha224& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(sha224)) >= 0;
}
bool operator>(const sha224& h1, const sha224& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(sha224)) > 0;
}
bool operator<(const sha224& h1, const sha224& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(sha224)) < 0;
}
bool operator!=(const sha224& h1, const sha224& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(sha224)) != 0;
}
bool operator==(const sha224& h1, const sha224& h2) {
   return memcmp(h1._hash, h2._hash, sizeof(sha224)) == 0;
}

void to_variant(const sha224& bi, variant& v) {
   v = std::vector<char>((const char*)&bi, ((const char*)&bi) + sizeof(bi));
}
void from_variant(const variant& v, sha224& bi) {
   std::vector<char> ve = v.as<std::vector<char>>();
   if (ve.size()) {
      memcpy(bi.data(), ve.data(), fcl::min<size_t>(ve.size(), sizeof(bi)));
   } else
      memset(bi.data(), char(0), sizeof(bi));
}

template <> unsigned int hmac<sha224>::internal_block_size() const {
   return 64;
}
} // namespace fcl::crypto
