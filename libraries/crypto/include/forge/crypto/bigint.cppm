module;
#include <stdint.h>
#include <openssl/types.h>
#include <span>
#include <string>

export module forge.crypto.bigint;

import forge.core.string;
import forge.crypto.types;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

export namespace forge::crypto {
class bigint {
 public:
   bigint(std::span<const std::uint8_t> bige);
   bigint(const std::uint8_t* bige, uint32_t l);
   bigint(uint64_t value);
   bigint();
   bigint(const bigint& c);
   bigint(bigint&& c);
   ~bigint();

   bigint& operator=(const bigint& a);
   bigint& operator=(bigint&& a);

   explicit operator bool() const;

   bool is_negative() const;
   int64_t to_int64() const;

   int64_t log2() const;
   bigint exp(const bigint& c) const;

   static bigint random(uint32_t bits, int t, int);

   bool operator<(const bigint& c) const;
   bool operator>(const bigint& c) const;
   bool operator>=(const bigint& c) const;
   bool operator==(const bigint& c) const;
   bool operator!=(const bigint& c) const;

   bigint operator+(const bigint& a) const;
   bigint operator*(const bigint& a) const;
   bigint operator/(const bigint& a) const;
   bigint operator%(const bigint& a) const;
   bigint operator/=(const bigint& a);
   bigint operator*=(const bigint& a);
   bigint& operator+=(const bigint& a);
   bigint& operator-=(const bigint& a);
   bigint& operator<<=(uint32_t i);
   bigint& operator>>=(uint32_t i);
   bigint operator-(const bigint& a) const;

   bigint operator++(int);
   bigint& operator++();
   bigint operator--(int);
   bigint& operator--();

   operator std::string() const;

   // returns bignum as bigendian bytes
   [[nodiscard]] bytes to_bytes() const;

 private:
   ::bignum_st* n;
};

/** encodes the big int as base64 string, or a number */
void to_variant(const bigint& bi, variant& v);
/** decodes the big int as base64 string, or a number */
void from_variant(const variant& v, bigint& bi);
} // namespace forge::crypto
