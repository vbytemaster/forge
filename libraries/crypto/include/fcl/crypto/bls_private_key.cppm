module;
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <boost/describe.hpp>
#include <span>
#include <string>

export module fcl.crypto.bls_private_key;

import fcl.crypto.bls_public_key;
import fcl.crypto.bls_signature;
import fcl.reflect.reflect;
import fcl.variant.described;
import fcl.variant;

export namespace fcl::crypto::blslib {

namespace config {
const std::string bls_private_key_prefix = "PVT_BLS_";
};

class bls_private_key {
 public:
   bls_private_key() = default;
   bls_private_key(bls_private_key&&) = default;
   bls_private_key(const bls_private_key&) = default;
   explicit bls_private_key(std::span<const uint8_t> seed) {
      _sk = bls12_381::secret_key(seed);
   }
   explicit bls_private_key(const std::string& base64urlstr);

   bls_private_key& operator=(const bls_private_key&) = default;

   std::string to_string() const;

   bls_public_key get_public_key() const;

   bls_signature sign(std::span<const uint8_t> msg) const;
   bls_signature proof_of_possession() const;

   static bls_private_key generate();

 private:
   std::array<uint64_t, 4> _sk;
   BOOST_DESCRIBE_CLASS(bls_private_key, (), (), (), (_sk))
   friend bool operator==(const bls_private_key& pk1, const bls_private_key& pk2);
}; // bls_private_key

void to_variant(const bls_private_key& var, variant& vo);
void from_variant(const variant& var, bls_private_key& vo);

} // namespace fcl::crypto::blslib
