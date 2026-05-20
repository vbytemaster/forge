module;
#include <fcl/core/macros.hpp>
#include <fcl/exception/macros.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/dynamic_bitset.hpp>
#include <array>
#include <chrono>
#include <map>
#include <deque>
#include <list>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

export module fcl.raw.raw;

import fcl.reflect.reflect;
import fcl.core.type_name;
import fcl.raw.datastream;
import fcl.raw.varint;
import fcl.core.utility;
import fcl.core.chrono;
import fcl.core.uint128;
import fcl.exception.exception;
import fcl.variant.static_variant;
import fcl.variant;
import fcl.variant.dynamic_bitset;

export namespace fcl::raw {

template <size_t Size>
using UInt = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
    Size, Size, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
template <size_t Size>
using Int = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
    Size, Size, boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
template <typename Stream> void pack(Stream& s, const UInt<256>& n);
template <typename Stream> void unpack(Stream& s, UInt<256>& n);
template <typename Stream> void pack(Stream& s, const Int<256>& n);
template <typename Stream> void unpack(Stream& s, Int<256>& n);
template <typename Stream, typename T> void pack(Stream& s, const boost::multiprecision::number<T>& n);
template <typename Stream, typename T> void unpack(Stream& s, boost::multiprecision::number<T>& n);
template <typename Stream> void pack(Stream& s, const fcl::dynamic_bitset& bs);
template <typename Stream> void unpack(Stream& s, fcl::dynamic_bitset& bs);
template <class T>
concept TrivialScalar = std::is_scalar_v<T> && !std::is_pointer_v<T>;

template <class T>
concept NotTrivialScalar = !TrivialScalar<T>;

template <typename T> size_t pack_size(const T& v);
template <typename Stream, typename T> void pack(Stream& s, const T& v);
template <typename Stream, typename T> void unpack(Stream& s, T& v);
template <typename Stream> void pack(Stream& s, const variant& v);
template <typename Stream> void unpack(Stream& s, variant& v);
template <typename Stream> void pack(Stream& s, const variant_object& v);
template <typename Stream> void unpack(Stream& s, variant_object& v);
template <typename Stream> void pack(Stream& s, const std::string&);
template <typename Stream> void unpack(Stream& s, std::string&);
template <typename Stream, typename T> void pack(Stream& s, const std::vector<T>& v);
template <typename Stream, typename T> void unpack(Stream& s, std::vector<T>& v);
template <typename Stream> void pack(Stream& s, const std::vector<char>& value);
template <typename Stream> void unpack(Stream& s, std::vector<char>& value);
template <typename Stream> void pack(Stream& s, const std::vector<std::uint8_t>& value);
template <typename Stream> void unpack(Stream& s, std::vector<std::uint8_t>& value);
template <typename Stream> void pack(Stream& s, const bool& v);
template <typename Stream> void unpack(Stream& s, bool& v);
template <typename Stream> void pack(Stream& s, const signed_int& v);
template <typename Stream> void unpack(Stream& s, signed_int& vi);
template <typename Stream> void pack(Stream& s, const unsigned_int& v);
template <typename Stream> void unpack(Stream& s, unsigned_int& vi);
template <typename Stream> void pack(Stream& s, const char* v);
template <typename Stream> void unpack(Stream& s, std::chrono::sys_time<std::chrono::microseconds>&);
template <typename Stream> void pack(Stream& s, const std::chrono::sys_time<std::chrono::microseconds>&);
template <typename Stream> void unpack(Stream& s, std::chrono::sys_seconds&);
template <typename Stream> void pack(Stream& s, const std::chrono::sys_seconds&);
template <typename Stream> void unpack(Stream& s, std::chrono::microseconds&);
template <typename Stream> void pack(Stream& s, const std::chrono::microseconds&);
template <typename Stream, TrivialScalar T, std::size_t N> void pack(Stream& s, const std::array<T, N>& v);
template <typename Stream, NotTrivialScalar T, std::size_t N> void pack(Stream& s, const std::array<T, N>& v);
template <typename Stream, TrivialScalar T, std::size_t N> void unpack(Stream& s, std::array<T, N>& v);
template <typename Stream, NotTrivialScalar T, std::size_t N> void unpack(Stream& s, std::array<T, N>& v);
template <typename Stream, typename... T> void pack(Stream& s, const std::variant<T...>& sv);
template <typename Stream, typename... T> void unpack(Stream& s, std::variant<T...>& sv);

template <typename Stream> class variant_packer : public variant::visitor {
 public:
   explicit variant_packer(Stream& _s) : s(_s) {}
   void handle() const override {}
   void handle(const int64_t& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const uint64_t& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const double& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const bool& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const std::string& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const variant_object& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const variants& v) const override {
      fcl::raw::pack(s, v);
   }
   void handle(const blob& v) const override {
      fcl::raw::pack(s, v);
   }

   Stream& s;
};

template <typename Stream> inline void pack(Stream& s, const variant& v) {
   pack(s, uint8_t(v.get_type()));
   v.visit(variant_packer<Stream>(s));
}

template <typename Stream> inline void unpack(Stream& s, variant& v) {
   uint8_t t;
   unpack(s, t);
   switch (t) {
   case variant::null_type:
      return;
   case variant::int64_type: {
      int64_t val;
      raw::unpack(s, val);
      v = val;
      return;
   }
   case variant::uint64_type: {
      uint64_t val;
      raw::unpack(s, val);
      v = val;
      return;
   }
   case variant::double_type: {
      double val;
      raw::unpack(s, val);
      v = val;
      return;
   }
   case variant::bool_type: {
      bool val;
      raw::unpack(s, val);
      v = val;
      return;
   }
   case variant::string_type: {
      std::string val;
      raw::unpack(s, val);
      v = std::move(val);
      return;
   }
   case variant::array_type: {
      variants val;
      raw::unpack(s, val);
      v = std::move(val);
      return;
   }
   case variant::object_type: {
      variant_object val;
      raw::unpack(s, val);
      v = std::move(val);
      return;
   }
   case variant::blob_type: {
      blob val;
      raw::unpack(s, val);
      v = std::move(val);
      return;
   }
   default:
      FCL_THROW("Unknown Variant Type ${t}", fcl::error::ctx("t", t));
   }
}

template <typename Stream> inline void pack(Stream& s, const variant_object& v) {
   unsigned_int vs = (uint32_t)v.size();
   pack(s, vs);
   for (auto itr = v.begin(); itr != v.end(); ++itr) {
      pack(s, itr->key());
      pack(s, itr->value());
   }
}

template <typename Stream> inline void unpack(Stream& s, variant_object& v) {
   unsigned_int vs;
   unpack(s, vs);

   mutable_variant_object mvo;
   mvo.reserve(vs.value);
   for (uint32_t i = 0; i < vs.value; ++i) {
      std::string key;
      fcl::variant value;
      fcl::raw::unpack(s, key);
      fcl::raw::unpack(s, value);
      mvo.set(std::move(key), std::move(value));
   }
   v = std::move(mvo);
}

template <typename Stream, typename Arg0, typename... Args>
inline void pack(Stream& s, const Arg0& a0, const Args&... args) {
   pack(s, a0);
   pack(s, args...);
}
template <typename Stream, typename Arg0, typename... Args> inline void unpack(Stream& s, Arg0& a0, Args&... args) {
   unpack(s, a0);
   unpack(s, args...);
}

template <typename Stream> inline void pack(Stream& s, const std::filesystem::path& tp) {
   fcl::raw::pack(s, tp.generic_string());
}

template <typename Stream> inline void unpack(Stream& s, std::filesystem::path& tp) {
   std::string p;
   fcl::raw::unpack(s, p);
   tp = p;
}

template <typename Stream> inline void pack(Stream& s, const std::chrono::sys_seconds& tp) {
   const uint32_t sec = fcl::chrono::to_fc_time_point_sec_wire(tp);
   s.write((const char*)&sec, sizeof(sec));
}

template <typename Stream> inline void unpack(Stream& s, std::chrono::sys_seconds& tp) {
   try {
      uint32_t sec;
      s.read((char*)&sec, sizeof(sec));
      tp = fcl::chrono::from_fc_time_point_sec_wire(sec);
   }
   FCL_CAPTURE_AND_RETHROW("")
}

template <typename Stream> inline void pack(Stream& s, const std::chrono::sys_time<std::chrono::microseconds>& tp) {
   const uint64_t usec = fcl::chrono::to_fc_time_point_wire(tp);
   s.write((const char*)&usec, sizeof(usec));
}

template <typename Stream> inline void unpack(Stream& s, std::chrono::sys_time<std::chrono::microseconds>& tp) {
   try {
      uint64_t usec;
      s.read((char*)&usec, sizeof(usec));
      tp = fcl::chrono::from_fc_time_point_wire(usec);
   }
   FCL_CAPTURE_AND_RETHROW("")
}

template <typename Stream> inline void pack(Stream& s, const std::chrono::microseconds& usec) {
   const uint64_t usec_as_int64 = fcl::chrono::to_fc_microseconds_wire(usec);
   s.write((const char*)&usec_as_int64, sizeof(usec_as_int64));
}

template <typename Stream> inline void unpack(Stream& s, std::chrono::microseconds& usec) {
   try {
      uint64_t usec_as_int64;
      s.read((char*)&usec_as_int64, sizeof(usec_as_int64));
      usec = fcl::chrono::from_fc_microseconds_wire(usec_as_int64);
   }
   FCL_CAPTURE_AND_RETHROW("")
}

template <typename Stream> inline void pack(Stream& s, const fcl::uint128& u) {
   s.write((const char*)&u, sizeof(u));
}

template <typename Stream> inline void unpack(Stream& s, fcl::uint128& u) {
   s.read((char*)&u, sizeof(u));
}

template <typename Stream, typename T, size_t N>
   requires(!std::is_same_v<std::remove_cv_t<T>, char>)
inline void pack(Stream& s, T (&v)[N]) {
   fcl::raw::pack(s, unsigned_int((uint32_t)N));
   for (uint64_t i = 0; i < N; ++i)
      fcl::raw::pack(s, v[i]);
}

template <typename Stream, typename T, size_t N>
   requires(!std::is_same_v<std::remove_cv_t<T>, char>)
inline void unpack(Stream& s, T (&v)[N]) {
   try {
      unsigned_int size;
      fcl::raw::unpack(s, size);
      FCL_ASSERT(size.value == N);
      for (uint64_t i = 0; i < N; ++i)
         fcl::raw::unpack(s, v[i]);
   }
   FCL_CAPTURE_AND_RETHROW("array unpack failed", fcl::error::ctx("type", fcl::get_typename<T>::name()),
                           fcl::error::ctx("length", N))
}

template <typename Stream, typename T> inline void pack(Stream& s, const std::shared_ptr<T>& v) {
   fcl::raw::pack(s, bool(!!v));
   if (!!v)
      fcl::raw::pack(s, *v);
}

template <typename Stream, typename T> inline void unpack(Stream& s, std::shared_ptr<T>& v) {
   try {
      bool b;
      fcl::raw::unpack(s, b);
      if (b) {
         // want to be able to unpack std::shared_ptr<const T>
         auto tmp = std::make_shared<std::remove_const_t<T>>();
         fcl::raw::unpack(s, *tmp);
         v = std::move(tmp);
      } else {
         v.reset();
      }
   }
   FCL_CAPTURE_AND_RETHROW("std::shared_ptr<T>", fcl::error::ctx("type", fcl::get_typename<T>::name()))
}

template <typename Stream> inline void pack(Stream& s, const signed_int& v) {
   uint32_t val = (v.value << 1) ^ (v.value >> 31); // apply zigzag encoding
   do {
      uint8_t b = uint8_t(val) & 0x7f;
      val >>= 7;
      b |= ((val > 0) << 7);
      s.write((char*)&b, 1); //.put(b);
   } while (val);
}

template <typename Stream> inline void pack(Stream& s, const unsigned_int& v) {
   uint64_t val = v.value;
   do {
      uint8_t b = uint8_t(val) & 0x7f;
      val >>= 7;
      b |= ((val > 0) << 7);
      s.write((char*)&b, 1); //.put(b);
   } while (val);
}

template <typename Stream> inline void unpack(Stream& s, signed_int& vi) {
   uint32_t v = 0;
   char b = 0;
   int by = 0;
   do {
      s.get(b);
      v |= uint32_t(uint8_t(b) & 0x7f) << by;
      by += 7;
   } while (uint8_t(b) & 0x80);
   vi.value = (v >> 1) ^ (~(v & 1) + 1ull); // reverse zigzag encoding
}

template <typename Stream> inline void unpack(Stream& s, unsigned_int& vi) {
   uint64_t v = 0;
   char b = 0;
   uint8_t by = 0;
   do {
      s.get(b);
      v |= uint32_t(uint8_t(b) & 0x7f) << by;
      by += 7;
   } while (uint8_t(b) & 0x80 && by < 32);
   vi.value = static_cast<uint32_t>(v);
}

template <typename Stream> inline void pack(Stream& s, const char* v) {
   size_t sz = std::strlen(v);
   FCL_ASSERT(sz <= MAX_SIZE_OF_BYTE_ARRAYS);
   fcl::raw::pack(s, unsigned_int(sz));
   if (sz)
      s.write(v, sz);
}

// optional
template <typename Stream, typename T> void pack(Stream& s, const std::optional<T>& v) {
   fcl::raw::pack(s, v.has_value());
   if (v)
      fcl::raw::pack(s, *v);
}

template <typename Stream, typename T> void unpack(Stream& s, std::optional<T>& v) {
   try {
      bool b;
      fcl::raw::unpack(s, b);
      if (b) {
         v = T();
         fcl::raw::unpack(s, *v);
      } else {
         v.reset();
      } // in case v has already has a value
   }
   FCL_CAPTURE_AND_RETHROW("optional<${type}>", fcl::error::ctx("type", fcl::get_typename<T>::name()))
}

namespace detail {

template <typename Stream, typename Byte> inline void pack_byte_vector(Stream& s, const std::vector<Byte>& value) {
   FCL_ASSERT(value.size() <= MAX_SIZE_OF_BYTE_ARRAYS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   if (value.size())
      s.write(reinterpret_cast<const char*>(value.data()), (uint32_t)value.size());
}

template <typename Stream, typename Byte> inline void unpack_byte_vector(Stream& s, std::vector<Byte>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_SIZE_OF_BYTE_ARRAYS);
   value.resize(size.value);
   if (value.size())
      s.read(reinterpret_cast<char*>(value.data()), value.size());
}

template <typename Container, typename T> inline void pack_to_container(Container& out, const T& value) {
   datastream<size_t> ps;
   fcl::raw::pack(ps, value);
   out.resize(ps.tellp());
   if (!out.empty()) {
      datastream<Container> ds(std::move(out));
      fcl::raw::pack(ds, value);
      out = std::move(ds.storage());
   }
}

} // namespace detail

// byte vectors are raw byte blobs, not element-wise vectors.
template <typename Stream> inline void pack(Stream& s, const std::vector<char>& value) {
   detail::pack_byte_vector(s, value);
}
template <typename Stream> inline void unpack(Stream& s, std::vector<char>& value) {
   detail::unpack_byte_vector(s, value);
}
template <typename Stream> inline void pack(Stream& s, const std::vector<std::uint8_t>& value) {
   detail::pack_byte_vector(s, value);
}
template <typename Stream> inline void unpack(Stream& s, std::vector<std::uint8_t>& value) {
   detail::unpack_byte_vector(s, value);
}

// fcl::string
template <typename Stream> inline void pack(Stream& s, const std::string& v) {
   FCL_ASSERT(v.size() <= MAX_SIZE_OF_BYTE_ARRAYS);
   fcl::raw::pack(s, unsigned_int((uint32_t)v.size()));
   if (v.size())
      s.write(v.c_str(), v.size());
}

template <typename Stream> inline void unpack(Stream& s, std::string& v) {
   std::vector<char> tmp;
   fcl::raw::unpack(s, tmp);
   if (tmp.size())
      v = std::string(tmp.data(), tmp.data() + tmp.size());
   else
      v = std::string();
}
// bool
template <typename Stream> inline void pack(Stream& s, const bool& v) {
   fcl::raw::pack(s, uint8_t(v));
}
template <typename Stream> inline void unpack(Stream& s, bool& v) {
   uint8_t b;
   fcl::raw::unpack(s, b);
   FCL_ASSERT((b & ~1) == 0);
   v = (b != 0);
}

namespace detail {

template <typename Stream, typename T> inline void pack_described_object(Stream& s, const T& v) {
   fcl::reflect::for_each_member<T>([&](const char*, auto member) { fcl::raw::pack(s, v.*member); });
}

template <typename Stream, typename T> inline void unpack_described_object(Stream& s, T& v) {
   fcl::reflect::for_each_member<std::remove_const_t<T>>([&](const char* name, auto member) {
      using member_type = std::remove_reference_t<decltype(v.*member)>;
      try {
         fcl::raw::unpack(s, const_cast<std::remove_const_t<member_type>&>(v.*member));
      }
      FCL_CAPTURE_AND_RETHROW("Error unpacking field ${field}", fcl::error::ctx("field", name))
   });
}

template <typename IsClass = fcl::true_type> struct if_class {
   template <typename Stream, typename T> static inline void pack(Stream& s, const T& v) {
      s << v;
   }
   template <typename Stream, typename T> static inline void unpack(Stream& s, T& v) {
      s >> v;
   }
};

template <> struct if_class<fcl::false_type> {
   template <typename Stream, typename T> static inline void pack(Stream& s, const T& v) {
      s.write((char*)&v, sizeof(v));
   }
   template <typename Stream, typename T> static inline void unpack(Stream& s, T& v) {
      s.read((char*)&v, sizeof(v));
   }
};

} // namespace detail

template <typename Stream, typename T> inline void pack(Stream& s, const std::unordered_set<T>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   auto itr = value.begin();
   auto end = value.end();
   while (itr != end) {
      fcl::raw::pack(s, *itr);
      ++itr;
   }
}
template <typename Stream, typename T> inline void unpack(Stream& s, std::unordered_set<T>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.clear();
   value.reserve(size.value);
   for (uint32_t i = 0; i < size.value; ++i) {
      T tmp;
      fcl::raw::unpack(s, tmp);
      value.insert(std::move(tmp));
   }
}

template <typename Stream, typename... Ts> inline void pack(Stream& s, const std::tuple<Ts...>& tup) {
   auto l = [&s](const auto&... v) { fcl::raw::pack(s, v...); };
   std::apply(l, tup);
}
template <typename Stream, typename... Ts> inline void unpack(Stream& s, std::tuple<Ts...>& tup) {
   auto l = [&s](auto&... v) { fcl::raw::unpack(s, v...); };
   std::apply(l, tup);
}

template <typename Stream, typename K, typename V> inline void pack(Stream& s, const std::pair<K, V>& value) {
   fcl::raw::pack(s, value.first);
   fcl::raw::pack(s, value.second);
}
template <typename Stream, typename K, typename V> inline void unpack(Stream& s, std::pair<K, V>& value) {
   fcl::raw::unpack(s, value.first);
   fcl::raw::unpack(s, value.second);
}

template <typename Stream, typename K, typename V> inline void pack(Stream& s, const std::unordered_map<K, V>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   auto itr = value.begin();
   auto end = value.end();
   while (itr != end) {
      fcl::raw::pack(s, *itr);
      ++itr;
   }
}
template <typename Stream, typename K, typename V> inline void unpack(Stream& s, std::unordered_map<K, V>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.clear();
   value.reserve(size.value);
   for (uint32_t i = 0; i < size.value; ++i) {
      std::pair<K, V> tmp;
      fcl::raw::unpack(s, tmp);
      value.insert(std::move(tmp));
   }
}
template <typename Stream, typename K, typename V> inline void pack(Stream& s, const std::map<K, V>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   auto itr = value.begin();
   auto end = value.end();
   while (itr != end) {
      fcl::raw::pack(s, *itr);
      ++itr;
   }
}
template <typename Stream, typename K, typename V> inline void unpack(Stream& s, std::map<K, V>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.clear();
   for (uint32_t i = 0; i < size.value; ++i) {
      std::pair<K, V> tmp;
      fcl::raw::unpack(s, tmp);
      value.insert(std::move(tmp));
   }
}

template <typename Stream, typename T> inline void pack(Stream& s, const std::deque<T>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   for (const auto& i : value) {
      fcl::raw::pack(s, i);
   }
}

template <typename Stream, typename T> inline void unpack(Stream& s, std::deque<T>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.resize(size.value);
   for (auto& i : value) {
      fcl::raw::unpack(s, i);
   }
}

template <typename Stream> inline void pack(Stream& s, const fcl::dynamic_bitset& value) {
   // pack the size of the bitset, not the number of blocks
   const auto num_blocks = value.num_blocks();
   FCL_ASSERT(num_blocks <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int(value.size()));
   [[maybe_unused]] constexpr size_t word_size = sizeof(fcl::dynamic_bitset::block_type) * CHAR_BIT;
   assert(num_blocks == (value.size() + word_size - 1) / word_size);
   // convert bitset to a vector of blocks
   std::vector<fcl::dynamic_bitset::block_type> blocks;
   blocks.resize(num_blocks);
   boost::to_block_range(value, blocks.begin());
   // pack the blocks
   for (const auto& b : blocks) {
      fcl::raw::pack(s, b);
   }
}

template <typename Stream> inline void unpack(Stream& s, fcl::dynamic_bitset& value) {
   // the packed size is the number of bits in the set, not the number of blocks
   unsigned_int size;
   fcl::raw::unpack(s, size);
   constexpr size_t word_size = sizeof(fcl::dynamic_bitset::block_type) * CHAR_BIT;
   size_t num_blocks = (size + word_size - 1) / word_size;
   FCL_ASSERT(num_blocks <= MAX_NUM_ARRAY_ELEMENTS);
   std::vector<fcl::dynamic_bitset::block_type> blocks(num_blocks);
   for (size_t i = 0; i < num_blocks; ++i) {
      fcl::raw::unpack(s, blocks[i]);
   }
   value = {blocks.cbegin(), blocks.cend()};
   value.resize(size.value);
}

template <typename Stream, typename T> inline void pack(Stream& s, const std::vector<T>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   for (const auto& i : value) {
      fcl::raw::pack(s, i);
   }
}

template <typename Stream, typename T> inline void unpack(Stream& s, std::vector<T>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.resize(size.value);
   for (auto& i : value) {
      fcl::raw::unpack(s, i);
   }
}

template <typename Stream, typename T> inline void pack(Stream& s, const std::list<T>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   for (const auto& i : value) {
      fcl::raw::pack(s, i);
   }
}

template <typename Stream, typename T> inline void unpack(Stream& s, std::list<T>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.clear();
   while (size.value--) {
      T i;
      fcl::raw::unpack(s, i);
      value.emplace_back(std::move(i));
   }
}

template <typename Stream, typename T> inline void pack(Stream& s, const std::set<T>& value) {
   FCL_ASSERT(value.size() <= MAX_NUM_ARRAY_ELEMENTS);
   fcl::raw::pack(s, unsigned_int((uint32_t)value.size()));
   auto itr = value.begin();
   auto end = value.end();
   while (itr != end) {
      fcl::raw::pack(s, *itr);
      ++itr;
   }
}

template <typename Stream, typename T> inline void unpack(Stream& s, std::set<T>& value) {
   unsigned_int size;
   fcl::raw::unpack(s, size);
   FCL_ASSERT(size.value <= MAX_NUM_ARRAY_ELEMENTS);
   value.clear();
   for (uint64_t i = 0; i < size.value; ++i) {
      T tmp;
      fcl::raw::unpack(s, tmp);
      value.insert(std::move(tmp));
   }
}

template <typename Stream, TrivialScalar T, std::size_t S> inline void pack(Stream& s, const std::array<T, S>& value) {
   static_assert(S <= MAX_NUM_ARRAY_ELEMENTS, "number of elements in array is too large");
   s.write((const char*)value.data(), S * sizeof(T));
}

template <typename Stream, NotTrivialScalar T, std::size_t S>
inline void pack(Stream& s, const std::array<T, S>& value) {
   static_assert(S <= MAX_NUM_ARRAY_ELEMENTS, "number of elements in array is too large");
   for (std::size_t i = 0; i < S; ++i) {
      fcl::raw::pack(s, value[i]);
   }
}

template <typename Stream, TrivialScalar T, std::size_t S> inline void unpack(Stream& s, std::array<T, S>& value) {
   static_assert(S <= MAX_NUM_ARRAY_ELEMENTS, "number of elements in array is too large");
   s.read((char*)value.data(), S * sizeof(T));
}

template <typename Stream, NotTrivialScalar T, std::size_t S> inline void unpack(Stream& s, std::array<T, S>& value) {
   static_assert(S <= MAX_NUM_ARRAY_ELEMENTS, "number of elements in array is too large");
   for (std::size_t i = 0; i < S; ++i) {
      fcl::raw::unpack(s, value[i]);
   }
}

template <typename Stream, typename T> inline void pack(Stream& s, const T& v) {
   using clean = std::remove_cv_t<T>;
   if constexpr (fcl::reflect::is_described_enum_v<clean>) {
      fcl::raw::pack(s, static_cast<int64_t>(v));
   } else if constexpr (fcl::reflect::is_described_object_v<clean>) {
      fcl::raw::detail::pack_described_object(s, v);
   } else {
      fcl::raw::detail::if_class<typename fcl::is_class<T>::type>::pack(s, v);
   }
}
template <typename Stream, typename T> inline void unpack(Stream& s, T& v) {
   try {
      using clean = std::remove_cv_t<T>;
      if constexpr (fcl::reflect::is_described_enum_v<clean>) {
         int64_t value = 0;
         fcl::raw::unpack(s, value);
         v = static_cast<T>(value);
      } else if constexpr (fcl::reflect::is_described_object_v<clean>) {
         fcl::raw::detail::unpack_described_object(s, v);
      } else {
         fcl::raw::detail::if_class<typename fcl::is_class<T>::type>::unpack(s, v);
      }
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::type_name<T>()))
}

template <typename T> inline size_t pack_size(const T& v) {
   datastream<size_t> ps;
   fcl::raw::pack(ps, v);
   return ps.tellp();
}

template <typename T> inline std::vector<char> pack(const T& v) {
   datastream<size_t> ps;
   fcl::raw::pack(ps, v);
   std::vector<char> vec(ps.tellp());

   if (vec.size()) {
      datastream<char*> ds(vec.data(), size_t(vec.size()));
      fcl::raw::pack(ds, v);
   }
   return vec;
}

template <typename T> inline void pack(std::vector<char>& out, const T& v) {
   detail::pack_to_container(out, v);
}

template <typename T> inline void pack(std::vector<std::uint8_t>& out, const T& v) {
   detail::pack_to_container(out, v);
}

template <typename T, typename... Next> inline std::vector<char> pack(const T& v, Next... next) {
   datastream<size_t> ps;
   fcl::raw::pack(ps, v, next...);
   std::vector<char> vec(ps.tellp());

   if (vec.size()) {
      datastream<char*> ds(vec.data(), size_t(vec.size()));
      fcl::raw::pack(ds, v, next...);
   }
   return vec;
}

template <typename T> inline T unpack(const std::vector<char>& s) {
   try {
      T tmp;
      datastream<const char*> ds(s.data(), size_t(s.size()));
      fcl::raw::unpack(ds, tmp);
      return tmp;
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::type_name<T>()))
}

template <typename T> inline T unpack(std::span<const std::uint8_t> s) {
   try {
      T tmp;
      datastream<const unsigned char*> ds(s.data(), size_t(s.size()));
      fcl::raw::unpack(ds, tmp);
      return tmp;
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::type_name<T>()))
}

template <typename T> inline void unpack(std::span<const std::uint8_t> s, T& tmp) {
   try {
      datastream<const unsigned char*> ds(s.data(), size_t(s.size()));
      fcl::raw::unpack(ds, tmp);
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::type_name<T>()))
}

template <typename T> inline T unpack(const std::vector<std::uint8_t>& s) {
   return fcl::raw::unpack<T>(std::span<const std::uint8_t>{s.data(), s.size()});
}

template <typename T> inline void unpack(const std::vector<std::uint8_t>& s, T& tmp) {
   fcl::raw::unpack(std::span<const std::uint8_t>{s.data(), s.size()}, tmp);
}

template <typename T> inline void unpack(const std::vector<char>& s, T& tmp) {
   try {
      datastream<const char*> ds(s.data(), size_t(s.size()));
      fcl::raw::unpack(ds, tmp);
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::type_name<T>()))
}

template <typename T> inline void pack(char* d, uint32_t s, const T& v) {
   datastream<char*> ds(d, s);
   fcl::raw::pack(ds, v);
}

template <typename T> inline T unpack(const char* d, uint32_t s) {
   try {
      T v;
      datastream<const char*> ds(d, s);
      fcl::raw::unpack(ds, v);
      return v;
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::get_typename<T>::name()))
}

template <typename T> inline void unpack(const char* d, uint32_t s, T& v) {
   try {
      datastream<const char*> ds(d, s);
      fcl::raw::unpack(ds, v);
   }
   FCL_CAPTURE_AND_RETHROW("error unpacking ${type}", fcl::error::ctx("type", fcl::get_typename<T>::name()))
}

template <typename Stream> struct pack_static_variant {
   Stream& stream;
   pack_static_variant(Stream& s) : stream(s) {}

   typedef void result_type;
   template <typename T> void operator()(const T& v) const {
      fcl::raw::pack(stream, v);
   }
};

template <typename Stream> struct unpack_static_variant {
   Stream& stream;
   unpack_static_variant(Stream& s) : stream(s) {}

   typedef void result_type;
   template <typename T> void operator()(T& v) const {
      fcl::raw::unpack(stream, v);
   }
};

template <typename Stream, typename... T> void pack(Stream& s, const std::variant<T...>& sv) {
   fcl::raw::pack(s, unsigned_int(sv.index()));
   std::visit(pack_static_variant<Stream>(s), sv);
}

template <typename Stream, typename... T> void unpack(Stream& s, std::variant<T...>& sv) {
   unsigned_int w;
   fcl::raw::unpack(s, w);
   fcl::from_index(sv, w.value);
   std::visit(unpack_static_variant<Stream>(s), sv);
}

template <typename Stream, typename T> void pack(Stream& s, const boost::multiprecision::number<T>& n) {
   static_assert(sizeof(n) == (std::numeric_limits<boost::multiprecision::number<T>>::digits + 1) / 8,
                 "unexpected padding");
   s.write((const char*)&n, sizeof(n));
}
template <typename Stream, typename T> void unpack(Stream& s, boost::multiprecision::number<T>& n) {
   static_assert(sizeof(n) == (std::numeric_limits<boost::multiprecision::number<T>>::digits + 1) / 8,
                 "unexpected padding");
   s.read((char*)&n, sizeof(n));
}

template <typename Stream> void pack(Stream& s, const UInt<256>& n) {
   pack(s, static_cast<UInt<128>>(n));
   pack(s, static_cast<UInt<128>>(n >> 128));
}
template <typename Stream> void unpack(Stream& s, UInt<256>& n) {
   UInt<128> tmp[2];
   unpack(s, tmp[0]);
   unpack(s, tmp[1]);
   n = tmp[1];
   n <<= 128;
   n |= tmp[0];
}

} // namespace fcl::raw
