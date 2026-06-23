#pragma once

#include <cstddef>

// Macro-only explicit-instantiation helpers for product/domain DTOs.
//
// C++ modules cannot export macros, so consumers include this header and import
// the relevant FORGE modules before expanding the macros:
//   import forge.variant;
//   import forge.raw.datastream;
//   import forge.raw.raw;
//   import forge.crypto.sha256; // required by FORGE_*_SERIALIZATION_PACK
//
// The macros intentionally do not declare new public types or functions.

#define FORGE_SERIALIZATION_VARIANT(ext, type)                                                                           \
   namespace forge {                                                                                                     \
   ext template void from_variant<type>(const variant& v, type& value);                                                \
   ext template void to_variant<type>(const type& value, variant& v);                                                  \
   }

#define FORGE_SERIALIZATION_PACK(ext, type)                                                                              \
   namespace forge::raw {                                                                                                \
   ext template void pack<forge::datastream<std::size_t>, type>(forge::datastream<std::size_t> & stream,                   \
                                                              const type& value);                                      \
   ext template void pack<forge::crypto::sha256::encoder, type>(forge::crypto::sha256::encoder & stream, const type& value);               \
   ext template void pack<forge::datastream<char*>, type>(forge::datastream<char*> & stream, const type& value);           \
   ext template void unpack<forge::datastream<const char*>, type>(forge::datastream<const char*> & stream, type& value);   \
   }

#define FORGE_SERIALIZATION(ext, type)                                                                                   \
   FORGE_SERIALIZATION_VARIANT(ext, type)                                                                                \
   FORGE_SERIALIZATION_PACK(ext, type)

#define FORGE_DECLARE_SERIALIZATION(type) FORGE_SERIALIZATION(extern, type)
#define FORGE_DECLARE_SERIALIZATION_VARIANT(type) FORGE_SERIALIZATION_VARIANT(extern, type)
#define FORGE_DECLARE_SERIALIZATION_PACK(type) FORGE_SERIALIZATION_PACK(extern, type)
#define FORGE_IMPLEMENT_SERIALIZATION(type) FORGE_SERIALIZATION(/* not extern */, type)
#define FORGE_IMPLEMENT_SERIALIZATION_VARIANT(type) FORGE_SERIALIZATION_VARIANT(/* not extern */, type)
#define FORGE_IMPLEMENT_SERIALIZATION_PACK(type) FORGE_SERIALIZATION_PACK(/* not extern */, type)
