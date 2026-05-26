module;
#include <fcl/exception/macros.hpp>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <iomanip>
#include <ios>
#include <ostream>
#include <span>
#include <string>

export module fcl.crypto.bls_public_key;

import fcl.variant.described;
import fcl.raw.varint;
import fcl.exception.exception;
import fcl.raw.raw;
import fcl.variant;

export namespace fcl::crypto::blslib {

namespace config {
const std::string bls_public_key_prefix = "PUB_BLS_";
};

// Immutable after construction (although operator= is provided).
//   Attributes are mutable so unpack can rebuild the cached Jacobian representation.
// Provides an efficient wrapper around bls12_381::g1.
// Serialization form:
//   Non-Montgomery form and little-endian encoding for the field elements.
//   Affine form for the group element (the z component is 1 and not included in the serialization).
//   Binary serialization encodes size(96), x component, followed by y component.
// Cached g1 in Jacobian Montgomery is used for efficient BLS math.
// Keeping the serialized data allows for efficient serialization without the expensive conversion
// from Jacobian Montgomery to Affine Non-Montgomery.
class bls_public_key {
 public:
   bls_public_key() = default;
   bls_public_key(bls_public_key&&) = default;
   bls_public_key(const bls_public_key&) = default;
   bls_public_key& operator=(const bls_public_key& rhs) = default;
   bls_public_key& operator=(bls_public_key&& rhs) = default;

   // throws if unable to convert to valid bls12_381::g1
   explicit bls_public_key(std::span<const uint8_t, 96> affine_non_montgomery_le);

   // affine non-montgomery base64url with bls_public_key_prefix
   explicit bls_public_key(const std::string& base64urlstr);

   // affine non-montgomery base64url with bls_public_key_prefix
   std::string to_string() const;

   const bls12_381::g1& jacobian_montgomery_le() const {
      return _jacobian_montgomery_le;
   }
   const std::array<uint8_t, 96>& affine_non_montgomery_le() const {
      return _affine_non_montgomery_le;
   }

   bool equal(const bls_public_key& pkey) const {
      return _jacobian_montgomery_le.equal(pkey._jacobian_montgomery_le);
   }

   auto operator<=>(const bls_public_key& rhs) const {
      return _affine_non_montgomery_le <=> rhs._affine_non_montgomery_le;
   }
   auto operator==(const bls_public_key& rhs) const {
      return _affine_non_montgomery_le == rhs._affine_non_montgomery_le;
   }

   template <typename T> friend T& operator<<(T& ds, const bls_public_key& sig) {
      // Serialization as variable length array when it is stored as a fixed length array. This makes for easier
      // deserialization by external tools
      fcl::raw::pack(ds, fcl::unsigned_int(static_cast<uint32_t>(sizeof(sig._affine_non_montgomery_le))));
      ds.write(reinterpret_cast<const char*>(sig._affine_non_montgomery_le.data()),
               sizeof(sig._affine_non_montgomery_le));
      return ds;
   }

   friend std::ostream& operator<<(std::ostream& os, const bls_public_key& k) {
      os << "bls_public_key(0x" << std::hex;
      for (auto c : k.affine_non_montgomery_le())
         os << std::setfill('0') << std::setw(2) << (int)c;
      os << std::dec << ")";
      return os;
   }

   template <typename T> friend T& operator>>(T& ds, bls_public_key& sig) {
      // Serialization as variable length array when it is stored as a fixed length array. This makes for easier
      // deserialization by external tools
      fcl::unsigned_int size;
      fcl::raw::unpack(ds, size);
      FCL_ASSERT(size.value == sizeof(sig._affine_non_montgomery_le));
      ds.read(reinterpret_cast<char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
      sig._jacobian_montgomery_le = from_affine_bytes_le(sig._affine_non_montgomery_le);
      return ds;
   }

   static bls12_381::g1 from_affine_bytes_le(const std::array<uint8_t, 96>& affine_non_montgomery_le);

 private:
   std::array<uint8_t, 96> _affine_non_montgomery_le{};
   bls12_381::g1 _jacobian_montgomery_le; // cached g1
};

void to_variant(const bls_public_key& var, variant& vo);
void from_variant(const variant& var, bls_public_key& vo);

} // namespace fcl::crypto::blslib
