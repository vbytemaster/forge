#pragma once

#include <cstddef>

// Macro-only explicit-instantiation helpers for product/domain DTOs.
//
// C++ modules cannot export macros, so consumers include this header and import
// the relevant FCL modules before expanding the macros:
//   import fcl.variant;
//   import fcl.raw.datastream;
//   import fcl.raw.raw;
//   import fcl.crypto.sha256; // required by FCL_*_SERIALIZATION_PACK
//
// The macros intentionally do not declare new public types or functions.

#define FCL_SERIALIZATION_VARIANT(ext, type)                                                                           \
   namespace fcl {                                                                                                     \
   ext template void from_variant<type>(const variant& v, type& value);                                                \
   ext template void to_variant<type>(const type& value, variant& v);                                                  \
   }

#define FCL_SERIALIZATION_PACK(ext, type)                                                                              \
   namespace fcl::raw {                                                                                                \
   ext template void pack<fcl::datastream<std::size_t>, type>(fcl::datastream<std::size_t> & stream,                   \
                                                              const type& value);                                      \
   ext template void pack<fcl::crypto::sha256::encoder, type>(fcl::crypto::sha256::encoder & stream, const type& value);               \
   ext template void pack<fcl::datastream<char*>, type>(fcl::datastream<char*> & stream, const type& value);           \
   ext template void unpack<fcl::datastream<const char*>, type>(fcl::datastream<const char*> & stream, type& value);   \
   }

#define FCL_SERIALIZATION(ext, type)                                                                                   \
   FCL_SERIALIZATION_VARIANT(ext, type)                                                                                \
   FCL_SERIALIZATION_PACK(ext, type)

#define FCL_DECLARE_SERIALIZATION(type) FCL_SERIALIZATION(extern, type)
#define FCL_DECLARE_SERIALIZATION_VARIANT(type) FCL_SERIALIZATION_VARIANT(extern, type)
#define FCL_DECLARE_SERIALIZATION_PACK(type) FCL_SERIALIZATION_PACK(extern, type)
#define FCL_IMPLEMENT_SERIALIZATION(type) FCL_SERIALIZATION(/* not extern */, type)
#define FCL_IMPLEMENT_SERIALIZATION_VARIANT(type) FCL_SERIALIZATION_VARIANT(/* not extern */, type)
#define FCL_IMPLEMENT_SERIALIZATION_PACK(type) FCL_SERIALIZATION_PACK(/* not extern */, type)
