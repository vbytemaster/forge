module;
#include <fcl/exception/macros.hpp>
#include <vector>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

module fcl.crypto.secp256k1;

namespace fcl::crypto::secp256k1 {

const secp256k1_context* recover_context() {
   static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
   return ctx;
}

recover_bytes recover(const recover_bytes& signature, const recover_bytes& digest) {
   const secp256k1_context* context{recover_context()};
   if (context == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::backend_error, "failed to initialize secp256k1 recovery context");
   }

   if (signature.size() != 65 || digest.size() != 32) {
      FCL_THROW_EXCEPTION(exceptions::invalid_input, "invalid secp256k1 recovery input size");
   }

   int recid = signature[0];
   if (recid < 27 || recid >= 35)
      FCL_THROW_EXCEPTION(exceptions::invalid_signature, "invalid secp256k1 recovery id");
   recid = (recid - 27) & 3;

   secp256k1_ecdsa_recoverable_signature sig;
   if (!secp256k1_ecdsa_recoverable_signature_parse_compact(context, &sig, (const unsigned char*)&signature[1],
                                                            recid)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_signature, "invalid secp256k1 recoverable signature");
   }

   secp256k1_pubkey pub_key;
   if (!secp256k1_ecdsa_recover(context, &pub_key, &sig, (const unsigned char*)&digest[0])) {
      FCL_THROW_EXCEPTION(exceptions::backend_error, "failed to recover secp256k1 public key");
   }

   size_t kOutLen{65};
   recover_bytes out(kOutLen, 0);
   secp256k1_ec_pubkey_serialize(context, out.data(), &kOutLen, &pub_key, SECP256K1_EC_UNCOMPRESSED);
   return out;
}
} // namespace fcl::crypto::secp256k1
