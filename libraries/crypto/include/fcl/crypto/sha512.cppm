module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

export module fcl.crypto.sha512;

import fcl.core.string;
export import fcl.crypto.digest;
import fcl.crypto.packhash;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

export namespace fcl::crypto {

class sha512 : public add_packhash_to_hash<sha512> {
 public:
   sha512();
   explicit sha512(const std::string& hex_str);

   std::string str() const;
   operator std::string() const;

   char* data();
   const char* data() const;
   size_t data_size() const {
      return 512 / 8;
   }

   std::span<const uint8_t> to_uint8_span() const {
      return {reinterpret_cast<const uint8_t*>(data()), reinterpret_cast<const uint8_t*>(data()) + data_size()};
   }

   static sha512 hash(const char* d, uint32_t dlen);
   static sha512 hash(std::span<const std::uint8_t> data);
   static sha512 hash(const std::string&);

   template <typename T> static sha512 hash(const T& t) {
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
      sha512 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   template <typename T> inline friend T& operator<<(T& ds, const sha512& ep) {
      ds.write(ep.data(), sizeof(ep));
      return ds;
   }

   template <typename T> inline friend T& operator>>(T& ds, sha512& ep) {
      ds.read(ep.data(), sizeof(ep));
      return ds;
   }
   friend sha512 operator<<(const sha512& h1, uint32_t i);
   friend bool operator==(const sha512& h1, const sha512& h2);
   friend bool operator!=(const sha512& h1, const sha512& h2);
   friend sha512 operator^(const sha512& h1, const sha512& h2);
   friend bool operator>=(const sha512& h1, const sha512& h2);
   friend bool operator>(const sha512& h1, const sha512& h2);
   friend bool operator<(const sha512& h1, const sha512& h2);

   uint64_t _hash[8];
};

typedef fcl::crypto::sha512 uint512;

void to_variant(const sha512& bi, variant& v);
void from_variant(const variant& v, sha512& bi);

} // namespace fcl::crypto
