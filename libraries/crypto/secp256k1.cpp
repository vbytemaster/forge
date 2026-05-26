module;
#include <fcl/exception/macros.hpp>
#include <array>
#include <exception>
#include <memory>
#include <openssl/rand.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <utility>
#if _WIN32
#include <malloc.h>
#elif defined(__FreeBSD__)
#include <stdlib.h>
#else
#include <alloca.h>
#endif

module fcl.crypto.secp256k1;

import fcl.crypto.hmac;
import fcl.crypto.openssl;
import fcl.crypto.rand;
import fcl.crypto.sha256;
import fcl.crypto.sha512;
import fcl.exception.exception;

#include "secp256k1_backend.hpp"

namespace fcl::crypto::secp256k1 {
namespace detail {
struct context_creator {
   context_creator() {
      ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
      char seed[32];
      rand_bytes(seed, sizeof(seed));
      FCL_ASSERT(secp256k1_context_randomize(ctx, (const unsigned char*)seed));
   }
   secp256k1_context* ctx = nullptr;
};
const secp256k1_context* _get_context() {
   static context_creator cc;
   return cc.ctx;
}

class public_key_impl {
 public:
   public_key_impl() noexcept {}

   public_key_impl(const public_key_impl& cpy) noexcept : _key(cpy._key) {}

   public_key_data _key;
};

typedef std::array<char, 37> chr37;
chr37 _derive_message(const public_key_data& key, int i);
fcl::crypto::sha256 _left(const fcl::crypto::sha512& v);
fcl::crypto::sha256 _right(const fcl::crypto::sha512& v);
const ec_group& get_curve();
const private_key_secret& get_curve_order();
const private_key_secret& get_half_curve_order();
} // namespace detail

static const public_key_data empty_pub{};

fcl::crypto::sha512 private_key::get_shared_secret(const public_key& other) const {
   static const private_key_secret empty_priv{};
   FCL_ASSERT(my->_key != empty_priv);
   FCL_ASSERT(other.my->_key != empty_pub);
   secp256k1_pubkey secp_pubkey;
   const auto other_serialized = other.serialize();
   FCL_ASSERT(secp256k1_ec_pubkey_parse(detail::_get_context(), &secp_pubkey,
                                        (const unsigned char*)other_serialized.data(), other_serialized.size()));
   FCL_ASSERT(secp256k1_ec_pubkey_tweak_mul(detail::_get_context(), &secp_pubkey, (unsigned char*)my->_key.data()));
   public_key_data serialized_result;
   size_t serialized_result_sz = sizeof(serialized_result);
   secp256k1_ec_pubkey_serialize(detail::_get_context(), reinterpret_cast<unsigned char*>(serialized_result.data()),
                                 &serialized_result_sz, &secp_pubkey, SECP256K1_EC_COMPRESSED);
   FCL_ASSERT(serialized_result_sz == sizeof(serialized_result));
   return fcl::crypto::sha512::hash(serialized_result.begin() + 1, serialized_result.size() - 1);
}

private_key private_key::generate() {
   private_key ret;
   do {
      rand_bytes(ret.my->_key.data(), ret.my->_key.data_size());
   } while (!secp256k1_ec_seckey_verify(detail::_get_context(), (const uint8_t*)ret.my->_key.data()));
   return ret;
}

public_key::public_key() : my(std::make_unique<detail::public_key_impl>()) {}

public_key::public_key(const public_key& pk)
    : my(pk.my ? std::make_unique<detail::public_key_impl>(*pk.my) : nullptr) {}

public_key::public_key(public_key&& pk) : my(std::move(pk.my)) {}

public_key::~public_key() {}

public_key& public_key::operator=(const public_key& pk) {
   my = pk.my ? std::make_unique<detail::public_key_impl>(*pk.my) : nullptr;
   return *this;
}

public_key& public_key::operator=(public_key&& pk) {
   my = std::move(pk.my);
   return *this;
}

bool public_key::valid() const {
   return my->_key != empty_pub;
}

public_key_data public_key::serialize() const {
   FCL_ASSERT(my->_key != empty_pub);
   return my->_key;
}

public_key::public_key(const public_key_point_data& dat) : my(std::make_unique<detail::public_key_impl>()) {
   const char* front = &dat.data()[0];
   if (*front == 0) {
   } else {
      secp256k1_pubkey pub;
      FCL_ASSERT(secp256k1_ec_pubkey_parse(detail::_get_context(), &pub, reinterpret_cast<const unsigned char*>(front),
                                           sizeof(dat)));
      size_t serialized_size = my->_key.size();
      FCL_ASSERT(secp256k1_ec_pubkey_serialize(detail::_get_context(),
                                               reinterpret_cast<unsigned char*>(my->_key.data()), &serialized_size,
                                               &pub, SECP256K1_EC_COMPRESSED));
      FCL_ASSERT(serialized_size == my->_key.size());
   }
}

public_key::public_key(const public_key_data& dat) : my(std::make_unique<detail::public_key_impl>()) {
   my->_key = dat;
}

public_key::public_key(const compact_signature& c, const fcl::crypto::sha256& digest, bool check_canonical)
    : my(std::make_unique<detail::public_key_impl>()) {
   int nV = c.data()[0];
   if (nV < 27 || nV >= 35)
      FCL_THROW("unable to reconstruct public key from signature");

   if (check_canonical) {
      FCL_ASSERT(is_canonical(c), "signature is not canonical");
   }

   secp256k1_pubkey secp_pub;
   secp256k1_ecdsa_recoverable_signature secp_sig;

   FCL_ASSERT(secp256k1_ecdsa_recoverable_signature_parse_compact(
       detail::_get_context(), &secp_sig, (unsigned char*)c.begin() + 1, (*c.begin() - 27) & 3));
   FCL_ASSERT(secp256k1_ecdsa_recover(detail::_get_context(), &secp_pub, &secp_sig, (unsigned char*)digest.data()));

   size_t serialized_result_sz = my->_key.size();
   secp256k1_ec_pubkey_serialize(detail::_get_context(), reinterpret_cast<unsigned char*>(my->_key.data()),
                                 &serialized_result_sz, &secp_pub, SECP256K1_EC_COMPRESSED);
   FCL_ASSERT(serialized_result_sz == my->_key.size());
}

} // namespace fcl::crypto::secp256k1
