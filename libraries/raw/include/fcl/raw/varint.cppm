module;
#include <stdint.h>
#include <unordered_map>

export module fcl.raw.varint;

import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

export namespace fcl {

struct unsigned_int {
   using base_uint = uint32_t;
   unsigned_int(base_uint v = 0) : value(v) {}

   template <typename T> unsigned_int(T v) : value(v) {}

   operator uint32_t() const {
      return value;
   }
   // operator uint64_t()const { return value; }

   unsigned_int& operator=(int32_t v) {
      value = v;
      return *this;
   }

   base_uint value;

   friend bool operator==(const unsigned_int& i, const base_uint& v) {
      return i.value == v;
   }
   friend bool operator==(const base_uint& i, const unsigned_int& v) {
      return i == v.value;
   }
   friend bool operator==(const unsigned_int& i, const unsigned_int& v) {
      return i.value == v.value;
   }

   friend bool operator!=(const unsigned_int& i, const base_uint& v) {
      return i.value != v;
   }
   friend bool operator!=(const base_uint& i, const unsigned_int& v) {
      return i != v.value;
   }
   friend bool operator!=(const unsigned_int& i, const unsigned_int& v) {
      return i.value != v.value;
   }

   friend bool operator<(const unsigned_int& i, const base_uint& v) {
      return i.value < v;
   }
   friend bool operator<(const base_uint& i, const unsigned_int& v) {
      return i < v.value;
   }
   friend bool operator<(const unsigned_int& i, const unsigned_int& v) {
      return i.value < v.value;
   }

   friend bool operator>=(const unsigned_int& i, const base_uint& v) {
      return i.value >= v;
   }
   friend bool operator>=(const base_uint& i, const unsigned_int& v) {
      return i >= v.value;
   }
   friend bool operator>=(const unsigned_int& i, const unsigned_int& v) {
      return i.value >= v.value;
   }
};

/**
 *  @brief serializes a 32 bit signed interger in as few bytes as possible
 *
 *  Uses the google protobuf algorithm for seralizing signed numbers
 */
struct signed_int {
   using base_int = int32_t;
   signed_int(base_int v = 0) : value(v) {}
   operator int32_t() const {
      return value;
   }
   template <typename T> signed_int& operator=(const T& v) {
      value = v;
      return *this;
   }
   signed_int operator++(int) {
      return value++;
   }
   signed_int& operator++() {
      ++value;
      return *this;
   }

   base_int value;

   friend bool operator==(const signed_int& i, const base_int& v) {
      return i.value == v;
   }
   friend bool operator==(const base_int& i, const signed_int& v) {
      return i == v.value;
   }
   friend bool operator==(const signed_int& i, const signed_int& v) {
      return i.value == v.value;
   }

   friend bool operator!=(const signed_int& i, const base_int& v) {
      return i.value != v;
   }
   friend bool operator!=(const base_int& i, const signed_int& v) {
      return i != v.value;
   }
   friend bool operator!=(const signed_int& i, const signed_int& v) {
      return i.value != v.value;
   }

   friend bool operator<(const signed_int& i, const base_int& v) {
      return i.value < v;
   }
   friend bool operator<(const base_int& i, const signed_int& v) {
      return i < v.value;
   }
   friend bool operator<(const signed_int& i, const signed_int& v) {
      return i.value < v.value;
   }

   friend bool operator>=(const signed_int& i, const base_int& v) {
      return i.value >= v;
   }
   friend bool operator>=(const base_int& i, const signed_int& v) {
      return i >= v.value;
   }
   friend bool operator>=(const signed_int& i, const signed_int& v) {
      return i.value >= v.value;
   }
};

void to_variant(const signed_int& var, variant& vo);
void from_variant(const variant& var, signed_int& vo);
void to_variant(const unsigned_int& var, variant& vo);
void from_variant(const variant& var, unsigned_int& vo);

} // namespace fcl

export namespace std {
template <> struct hash<fcl::signed_int> {
 public:
   size_t operator()(const fcl::signed_int& a) const {
      return std::hash<fcl::signed_int::base_int>()(a.value);
   }
};
template <> struct hash<fcl::unsigned_int> {
 public:
   size_t operator()(const fcl::signed_int& a) const {
      return std::hash<uint32_t>()(a.value);
   }
};
} // namespace std
