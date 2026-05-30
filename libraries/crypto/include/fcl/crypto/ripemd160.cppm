module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module fcl.crypto.ripemd160;

import fcl.crypto.packhash;
export import fcl.crypto.digest;
import fcl.raw.raw;
import fcl.core.type_name;
import fcl.variant;
import fcl.crypto.sha256;
import fcl.crypto.sha512;

export namespace fcl::crypto {

class ripemd160 : public add_packhash_to_hash<ripemd160> {
 public:
   ripemd160();
   explicit ripemd160(const std::string& hex_str);

   std::string str() const;
   explicit operator std::string() const;

   char* data() const;
   size_t data_size() const {
      return 160 / 8;
   }

   static ripemd160 hash(const fcl::crypto::sha512& h);
   static ripemd160 hash(const fcl::crypto::sha256& h);
   static ripemd160 hash(const char* d, uint32_t dlen);
   static ripemd160 hash(const std::string&);

   template <typename T> static ripemd160 hash(const T& t) {
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
      ripemd160 result();

    private:
      struct impl;
      std::unique_ptr<impl> my;
   };

   template <typename T> inline friend T& operator<<(T& ds, const ripemd160& ep) {
      ds.write(ep.data(), sizeof(ep));
      return ds;
   }

   template <typename T> inline friend T& operator>>(T& ds, ripemd160& ep) {
      ds.read(ep.data(), sizeof(ep));
      return ds;
   }
   friend ripemd160 operator<<(const ripemd160& h1, uint32_t i);
   friend bool operator==(const ripemd160& h1, const ripemd160& h2);
   friend bool operator!=(const ripemd160& h1, const ripemd160& h2);
   friend ripemd160 operator^(const ripemd160& h1, const ripemd160& h2);
   friend bool operator>=(const ripemd160& h1, const ripemd160& h2);
   friend bool operator>(const ripemd160& h1, const ripemd160& h2);
   friend bool operator<(const ripemd160& h1, const ripemd160& h2);

   uint32_t _hash[5];
};

void to_variant(const ripemd160& bi, variant& v);
void from_variant(const variant& v, ripemd160& bi);

typedef ripemd160 uint160_t;
typedef ripemd160 uint160;

} // namespace fcl::crypto

export template <> struct fcl::get_typename<fcl::crypto::uint160_t> {
   static const char* name() {
      return "uint160_t";
   }
};

export namespace std {
template <> struct hash<fcl::crypto::ripemd160> {
   size_t operator()(const fcl::crypto::ripemd160& s) const {
      return *((size_t*)&s);
   }
};
} // namespace std
