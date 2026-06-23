module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

export module forge.crypto.sha224;

import forge.crypto.packhash;
export import forge.crypto.digest;
import forge.raw.raw;
import forge.core.string;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

export namespace forge::crypto {

class sha224 : public add_packhash_to_hash<sha224> {
 public:
   sha224();
   explicit sha224(const std::string& hex_str);

   std::string str() const;
   operator std::string() const;

   char* data();
   const char* data() const;
   size_t data_size() const {
      return 224 / 8;
   }

   static sha224 hash(const char* d, uint32_t dlen);
   static sha224 hash(const std::string&);

   template <typename T> static sha224 hash(const T& t) {
      return packhash(t);
   }

   class encoder {
    public:
      encoder();
      ~encoder();

      void write(const char* d, uint32_t dlen);
      void put(char c) {
         write(&c, 1);
      }
      void reset();
      sha224 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   template <typename T> inline friend T& operator<<(T& ds, const sha224& ep) {
      static_assert(sizeof(ep) == (8 * 3 + 4), "sha224 size mismatch");
      ds.write(ep.data(), sizeof(ep));
      return ds;
   }

   template <typename T> inline friend T& operator>>(T& ds, sha224& ep) {
      ds.read(ep.data(), sizeof(ep));
      return ds;
   }
   friend sha224 operator<<(const sha224& h1, uint32_t i);
   friend bool operator==(const sha224& h1, const sha224& h2);
   friend bool operator!=(const sha224& h1, const sha224& h2);
   friend sha224 operator^(const sha224& h1, const sha224& h2);
   friend bool operator>=(const sha224& h1, const sha224& h2);
   friend bool operator>(const sha224& h1, const sha224& h2);
   friend bool operator<(const sha224& h1, const sha224& h2);
   friend std::size_t hash_value(const sha224& v) {
      return uint64_t(v._hash[1]) << 32 | v._hash[2];
   }

   uint32_t _hash[7];
};

void to_variant(const sha224& bi, variant& v);
void from_variant(const variant& v, sha224& bi);

} // namespace forge::crypto
export namespace std {
template <> struct hash<forge::crypto::sha224> {
   size_t operator()(const forge::crypto::sha224& s) const {
      return *((size_t*)&s);
   }
};
} // namespace std
