module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module forge.crypto.sha1;

import forge.core.string;
export import forge.crypto.digest;
import forge.crypto.packhash;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

export namespace forge::crypto {

class sha1 : public add_packhash_to_hash<sha1> {
 public:
   sha1();
   explicit sha1(const std::string& hex_str);

   std::string str() const;
   operator std::string() const;

   char* data();
   const char* data() const;
   size_t data_size() const {
      return 20;
   }

   static sha1 hash(const char* d, uint32_t dlen);
   static sha1 hash(const std::string&);

   template <typename T> static sha1 hash(const T& t) {
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
      sha1 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   template <typename T> inline friend T& operator<<(T& ds, const sha1& ep) {
      ds.write(ep.data(), sizeof(ep));
      return ds;
   }

   template <typename T> inline friend T& operator>>(T& ds, sha1& ep) {
      ds.read(ep.data(), sizeof(ep));
      return ds;
   }
   friend sha1 operator<<(const sha1& h1, uint32_t i);
   friend bool operator==(const sha1& h1, const sha1& h2);
   friend bool operator!=(const sha1& h1, const sha1& h2);
   friend sha1 operator^(const sha1& h1, const sha1& h2);
   friend bool operator>=(const sha1& h1, const sha1& h2);
   friend bool operator>(const sha1& h1, const sha1& h2);
   friend bool operator<(const sha1& h1, const sha1& h2);

   uint32_t _hash[5];
};

void to_variant(const sha1& bi, variant& v);
void from_variant(const variant& v, sha1& bi);

} // namespace forge::crypto

export namespace std {
template <> struct hash<forge::crypto::sha1> {
   size_t operator()(const forge::crypto::sha1& s) const {
      return *((size_t*)&s);
   }
};
} // namespace std
