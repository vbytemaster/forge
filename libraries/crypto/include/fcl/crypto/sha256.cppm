module;
#include <fcl/exception/macros.hpp>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <functional>
#include <memory>
#include <string>
#include <span>
#include <compare>
#include <boost/functional/hash.hpp>

export module fcl.crypto.sha256;

import fcl.core.string;
export import fcl.crypto.digest;
import fcl.crypto.packhash;
import fcl.raw.raw;
import fcl.variant;

export namespace fcl::crypto {

class sha256 : public add_packhash_to_hash<sha256> {
 public:
   sha256();
   explicit sha256(const std::string& hex_str);
   explicit sha256(const char* data, size_t size);

   std::string str() const;
   operator std::string() const;

   const char* data() const;
   char* data();
   size_t data_size() const {
      return 256 / 8;
   }

   std::span<const uint8_t> to_uint8_span() const {
      return {reinterpret_cast<const uint8_t*>(data()), reinterpret_cast<const uint8_t*>(data()) + data_size()};
   }

   bool empty() const {
      return (_hash[0] | _hash[1] | _hash[2] | _hash[3]) == 0;
   }

   static sha256 hash(const char* d, uint32_t dlen);
   static sha256 hash(std::span<const std::uint8_t> data);
   static sha256 hash(const std::string&);
   static sha256 hash(const sha256&);

   template <typename T> static sha256 hash(const T& t) {
      return packhash(t);
   }

   class encoder {
    public:
      encoder();
      ~encoder();

      void write(const char* d, uint32_t dlen);
      void write(std::span<const std::uint8_t> data);
      void put(char c) {
         write(&c, 1);
      }
      void reset();
      sha256 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   template <typename T> inline friend T& operator<<(T& ds, const sha256& ep) {
      ds.write(ep.data(), sizeof(ep));
      return ds;
   }

   template <typename T> inline friend T& operator>>(T& ds, sha256& ep) {
      ds.read(ep.data(), sizeof(ep));
      return ds;
   }
   friend sha256 operator<<(const sha256& h1, uint32_t i);
   friend sha256 operator>>(const sha256& h1, uint32_t i);
   friend sha256 operator^(const sha256& h1, const sha256& h2);

   friend bool operator==(const sha256& h1, const sha256& h2);
   friend std::strong_ordering operator<=>(const sha256& h1, const sha256& h2);

   uint32_t pop_count() const {
      return static_cast<uint32_t>(std::popcount(_hash[0]) + std::popcount(_hash[1]) + std::popcount(_hash[2]) +
                                   std::popcount(_hash[3]));
   }

   /**
    * Count leading zero bits
    */
   uint16_t clz() const;

   /**
    * Approximate (log_2(x) + 1) * 2**24.
    *
    * Detailed specs:
    * - Return 0 when x == 0.
    * - High 8 bits of result simply counts nonzero bits.
    * - Low 24 bits of result are the 24 bits of input immediately after the most significant 1 in the input.
    * - If above would require reading beyond the end of the input, zeros are used instead.
    */
   uint32_t approx_log_32() const;

   void set_to_inverse_approx_log_32(uint32_t x);
   static double inverse_approx_log_32_double(uint32_t x);

   uint64_t _hash[4];
};

typedef sha256 uint256;

void to_variant(const sha256& bi, variant& v);
void from_variant(const variant& v, sha256& bi);

uint64_t hash64(const char* buf, size_t len);

inline size_t hash_value(const fcl::crypto::sha256& s) {
   return s._hash[3];
}

} // namespace fcl::crypto

export namespace std {
template <> struct hash<fcl::crypto::sha256> {
   size_t operator()(const fcl::crypto::sha256& s) const {
      return *((size_t*)&s);
   }
};

inline std::ostream& operator<<(std::ostream& os, const fcl::crypto::sha256& r) {
   os << "sha256(" << r.str() << ")";
   return os;
}

} // namespace std

export namespace boost {
template <> struct hash<fcl::crypto::sha256> {
   size_t operator()(const fcl::crypto::sha256& s) const {
      return s._hash[3]; //*((size_t*)&s);
   }
};
} // namespace boost
