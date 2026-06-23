module;
#include <forge/exceptions/macros.hpp>
#include "openssl_backend.hpp"
#include <openssl/core_names.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/params.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <vector>

module forge.crypto.p256;

import forge.crypto.random;
import forge.crypto.sha256;
import forge.crypto.sha512;
import forge.exceptions;

namespace forge::crypto::p256 {
namespace detail {
class public_key_impl {
 public:
   public_key_data _key;
};

class private_key_impl {
 public:
   private_key_secret _key;
};
} // namespace detail

namespace {
using forge::crypto::detail::bn_ctx;
using forge::crypto::detail::ec_group;
using forge::crypto::detail::ec_point;
using forge::crypto::detail::ecdsa_sig;
using forge::crypto::detail::ssl_bignum;

constexpr const char* r1_group_name = "prime256v1";

const public_key_data empty_public_key{};
const private_key_secret empty_private_key{};

struct evp_pkey_deleter {
   void operator()(EVP_PKEY* p) const noexcept {
      EVP_PKEY_free(p);
   }
};

struct evp_pkey_ctx_deleter {
   void operator()(EVP_PKEY_CTX* p) const noexcept {
      EVP_PKEY_CTX_free(p);
   }
};

struct ossl_param_deleter {
   void operator()(OSSL_PARAM* p) const noexcept {
      OSSL_PARAM_free(p);
   }
};

struct ossl_param_bld_deleter {
   void operator()(OSSL_PARAM_BLD* p) const noexcept {
      OSSL_PARAM_BLD_free(p);
   }
};

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;
using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter>;
using ossl_param_ptr = std::unique_ptr<OSSL_PARAM, ossl_param_deleter>;
using ossl_param_bld_ptr = std::unique_ptr<OSSL_PARAM_BLD, ossl_param_bld_deleter>;

const ec_group& get_curve() {
   static const ec_group group(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
   return group;
}

void raise_openssl_failure(const char* message) {
   FORGE_THROW_EXCEPTION(exceptions::backend_error, "OpenSSL error", forge::exceptions::ctx("message", message),
                       forge::exceptions::ctx("code", static_cast<uint32_t>(ERR_get_error())));
}

bool is_empty(const public_key_data& key) {
   return key == empty_public_key;
}

bool is_empty(const private_key_secret& key) {
   return key == empty_private_key;
}

ssl_bignum bignum_from_bytes(const void* data, size_t size) {
   ssl_bignum bn;
   if (BN_bin2bn(reinterpret_cast<const unsigned char*>(data), static_cast<int>(size), bn) == nullptr)
      raise_openssl_failure("error constructing BIGNUM");
   return bn;
}

bool valid_secret(const private_key_secret& secret) {
   if (is_empty(secret))
      return false;
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum order;
   FORGE_ASSERT(EC_GROUP_get_order(group, order, ctx));
   ssl_bignum value = bignum_from_bytes(secret.data(), secret.data_size());
   return !BN_is_zero(value) && BN_cmp(value, order) < 0;
}

public_key_data point_to_public_key_data(const EC_POINT* point) {
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   public_key_data result;
   const auto written = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
                                           reinterpret_cast<unsigned char*>(result.data()), result.size(), ctx);
   FORGE_ASSERT(written == result.size(), "unexpected P-256 public key size", forge::exceptions::ctx("written", written),
              forge::exceptions::ctx("expected", result.size()));
   return result;
}

public_key_point_data point_to_uncompressed_data(const public_key_data& data) {
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ec_point point(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_oct2point(group, point, reinterpret_cast<const unsigned char*>(data.data()), data.size(), ctx));
   public_key_point_data result;
   const auto written = EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED,
                                           reinterpret_cast<unsigned char*>(result.data()), result.size(), ctx);
   FORGE_ASSERT(written == result.size(), "unexpected uncompressed P-256 public key size",
              forge::exceptions::ctx("written", written), forge::exceptions::ctx("expected", result.size()));
   return result;
}

public_key_data normalize_public_key_data(const void* data, size_t size) {
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ec_point point(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_oct2point(group, point, reinterpret_cast<const unsigned char*>(data), size, ctx));
   return point_to_public_key_data(point);
}

public_key_data derive_public_key_data(const private_key_secret& secret) {
   FORGE_ASSERT(valid_secret(secret), "invalid P-256 private key");
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum priv = bignum_from_bytes(secret.data(), secret.data_size());
   ec_point point(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_mul(group, point, priv, nullptr, nullptr, ctx));
   return point_to_public_key_data(point);
}

evp_pkey_ptr make_public_pkey(const public_key_data& pub) {
   auto pub_uncompressed = point_to_uncompressed_data(pub);
   auto group_name = std::array<char, 16>{};
   std::strncpy(group_name.data(), r1_group_name, group_name.size() - 1);
   OSSL_PARAM params[] = {
       OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group_name.data(), 0),
       OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, pub_uncompressed.data(), pub_uncompressed.size()),
       OSSL_PARAM_construct_end()};
   evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
   if (!ctx || 1 != EVP_PKEY_fromdata_init(ctx.get()))
      raise_openssl_failure("error initializing EVP public key context");
   EVP_PKEY* raw = nullptr;
   if (1 != EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_PUBLIC_KEY, params))
      raise_openssl_failure("error constructing EVP public key");
   return evp_pkey_ptr(raw);
}

evp_pkey_ptr make_private_pkey(const private_key_secret& secret) {
   auto pub = derive_public_key_data(secret);
   auto pub_uncompressed = point_to_uncompressed_data(pub);
   ssl_bignum priv = bignum_from_bytes(secret.data(), secret.data_size());
   ossl_param_bld_ptr builder(OSSL_PARAM_BLD_new());
   FORGE_ASSERT(builder != nullptr, "error allocating EVP private key params");
   FORGE_ASSERT(1 == OSSL_PARAM_BLD_push_utf8_string(builder.get(), OSSL_PKEY_PARAM_GROUP_NAME, r1_group_name, 0));
   FORGE_ASSERT(1 == OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_PRIV_KEY, priv));
   FORGE_ASSERT(1 == OSSL_PARAM_BLD_push_octet_string(builder.get(), OSSL_PKEY_PARAM_PUB_KEY, pub_uncompressed.data(),
                                                    pub_uncompressed.size()));
   ossl_param_ptr params(OSSL_PARAM_BLD_to_param(builder.get()));
   FORGE_ASSERT(params != nullptr, "error building EVP private key params");
   evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
   if (!ctx || 1 != EVP_PKEY_fromdata_init(ctx.get()))
      raise_openssl_failure("error initializing EVP private key context");
   EVP_PKEY* raw = nullptr;
   if (1 != EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_KEYPAIR, params.get()))
      raise_openssl_failure("error constructing EVP private key");
   return evp_pkey_ptr(raw);
}

std::vector<unsigned char> sign_der(const private_key_secret& secret, const forge::crypto::sha256& digest) {
   auto key = make_private_pkey(secret);
   evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new(key.get(), nullptr));
   if (!ctx || 1 != EVP_PKEY_sign_init(ctx.get()))
      raise_openssl_failure("error initializing EVP P-256 signer");
   size_t sig_len = 0;
   if (1 != EVP_PKEY_sign(ctx.get(), nullptr, &sig_len, reinterpret_cast<const unsigned char*>(digest.data()),
                          digest.data_size()))
      raise_openssl_failure("error sizing EVP P-256 signature");
   std::vector<unsigned char> sig(sig_len);
   if (1 != EVP_PKEY_sign(ctx.get(), sig.data(), &sig_len, reinterpret_cast<const unsigned char*>(digest.data()),
                          digest.data_size()))
      raise_openssl_failure("error creating EVP P-256 signature");
   sig.resize(sig_len);
   return sig;
}

ecdsa_sig parse_der_signature(const std::vector<unsigned char>& der) {
   const unsigned char* cursor = der.data();
   ecdsa_sig sig(d2i_ECDSA_SIG(nullptr, &cursor, der.size()));
   if (!sig)
      raise_openssl_failure("error parsing DER ECDSA signature");
   return sig;
}

size_t der_signature_size(const signature& sig) {
   const auto* bytes = reinterpret_cast<const unsigned char*>(sig.data());
   if (sig.size() < 2 || bytes[0] != 0x30)
      return 0;
   if ((bytes[1] & 0x80) == 0)
      return static_cast<size_t>(bytes[1]) + 2;
   const auto length_bytes = static_cast<size_t>(bytes[1] & 0x7f);
   if (length_bytes == 0 || length_bytes > sizeof(size_t) || 2 + length_bytes > sig.size())
      return 0;
   size_t payload_size = 0;
   for (size_t i = 0; i < length_bytes; ++i)
      payload_size = (payload_size << 8) | bytes[2 + i];
   return 2 + length_bytes + payload_size;
}

void require_der_signature(std::span<const std::uint8_t> der) {
   const auto* cursor = der.data();
   const auto* end = der.data() + der.size();
   ecdsa_sig sig(d2i_ECDSA_SIG(nullptr, &cursor, static_cast<long>(der.size())));
   if (!sig || cursor != end) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "invalid P-256 DER signature");
   }
}

std::optional<public_key_data> recover_public_key_from_sig(ECDSA_SIG* sig, const forge::crypto::sha256& digest, int recid,
                                                           bool check) {
   const BIGNUM *r = nullptr, *s = nullptr;
   ECDSA_SIG_get0(sig, &r, &s);
   FORGE_ASSERT(r != nullptr && s != nullptr, "invalid P-256 signature");

   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   BN_CTX_start(ctx);
   EC_POINT* R = nullptr;
   EC_POINT* O = nullptr;
   EC_POINT* Q = nullptr;
   try {
      BIGNUM* order = BN_CTX_get(ctx);
      BIGNUM* x = BN_CTX_get(ctx);
      BIGNUM* field = BN_CTX_get(ctx);
      BIGNUM* e = BN_CTX_get(ctx);
      BIGNUM* zero = BN_CTX_get(ctx);
      BIGNUM* rr = BN_CTX_get(ctx);
      BIGNUM* sor = BN_CTX_get(ctx);
      BIGNUM* eor = BN_CTX_get(ctx);
      FORGE_ASSERT(eor != nullptr, "error allocating P-256 recovery bignums");
      FORGE_ASSERT(EC_GROUP_get_order(group, order, ctx));
      FORGE_ASSERT(BN_copy(x, order));
      FORGE_ASSERT(BN_mul_word(x, recid / 2));
      FORGE_ASSERT(BN_add(x, x, r));
      FORGE_ASSERT(EC_GROUP_get_curve(group, field, nullptr, nullptr, ctx));
      if (BN_cmp(x, field) >= 0) {
         BN_CTX_end(ctx);
         return std::nullopt;
      }
      R = EC_POINT_new(group);
      FORGE_ASSERT(R != nullptr, "error allocating P-256 recovery point");
      if (!EC_POINT_set_compressed_coordinates(group, R, x, recid % 2, ctx)) {
         EC_POINT_free(R);
         BN_CTX_end(ctx);
         return std::nullopt;
      }
      if (check) {
         O = EC_POINT_new(group);
         FORGE_ASSERT(O != nullptr, "error allocating P-256 recovery check point");
         FORGE_ASSERT(EC_POINT_mul(group, O, nullptr, R, order, ctx));
         if (!EC_POINT_is_at_infinity(group, O)) {
            EC_POINT_free(R);
            EC_POINT_free(O);
            BN_CTX_end(ctx);
            return std::nullopt;
         }
      }
      Q = EC_POINT_new(group);
      FORGE_ASSERT(Q != nullptr, "error allocating P-256 recovered point");
      const int degree = EC_GROUP_get_degree(group);
      FORGE_ASSERT(BN_bin2bn(reinterpret_cast<const unsigned char*>(digest.data()), digest.data_size(), e));
      if (8 * static_cast<int>(digest.data_size()) > degree)
         BN_rshift(e, e, 8 - (degree & 7));
      BN_zero(zero);
      FORGE_ASSERT(BN_mod_sub(e, zero, e, order, ctx));
      FORGE_ASSERT(BN_mod_inverse(rr, r, order, ctx));
      FORGE_ASSERT(BN_mod_mul(sor, s, rr, order, ctx));
      FORGE_ASSERT(BN_mod_mul(eor, e, rr, order, ctx));
      FORGE_ASSERT(EC_POINT_mul(group, Q, eor, R, sor, ctx));
      auto result = point_to_public_key_data(Q);
      EC_POINT_free(R);
      EC_POINT_free(O);
      EC_POINT_free(Q);
      BN_CTX_end(ctx);
      return result;
   } catch (...) {
      if (R != nullptr)
         EC_POINT_free(R);
      if (O != nullptr)
         EC_POINT_free(O);
      if (Q != nullptr)
         EC_POINT_free(Q);
      BN_CTX_end(ctx);
      throw;
   }
}
} // namespace

public_key_data recover_public_key_data(const compact_signature& c, const forge::crypto::sha256& digest, bool check_canonical) {
   int nV = c.data()[0];
   if (nV < 27 || nV >= 35)
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "unable to reconstruct public key from signature");
   ecdsa_sig sig(ECDSA_SIG_new());
   BIGNUM* r = BN_new();
   BIGNUM* s = BN_new();
   FORGE_ASSERT(r != nullptr && s != nullptr, "error allocating P-256 signature bignums");
   BN_bin2bn(&c.data()[1], 32, r);
   BN_bin2bn(&c.data()[33], 32, s);
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum order, halforder;
   FORGE_ASSERT(EC_GROUP_get_order(group, order, ctx));
   BN_rshift1(halforder, order);
   if (check_canonical && BN_cmp(s, halforder) > 0)
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "invalid high s-value encountered in P-256 signature");
   ECDSA_SIG_set0(sig, r, s);
   if (nV >= 31)
      nV -= 4;
   auto recovered = recover_public_key_from_sig(sig, digest, nV - 27, false);
   if (!recovered)
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "unable to reconstruct public key from signature");
   return *recovered;
}

compact_signature signature_from_ecdsa(const public_key_data& pub_data, forge::crypto::detail::ecdsa_sig& sig,
                                       const forge::crypto::sha256& d) {
   const BIGNUM *sig_r = nullptr, *sig_s = nullptr;
   ECDSA_SIG_get0(sig, &sig_r, &sig_s);
   BIGNUM* r = BN_dup(sig_r);
   BIGNUM* s = BN_dup(sig_s);
   FORGE_ASSERT(r != nullptr && s != nullptr, "error copying P-256 signature bignums");

   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum order, halforder;
   EC_GROUP_get_order(group, order, ctx);
   BN_rshift1(halforder, order);
   if (BN_cmp(s, halforder) > 0)
      BN_sub(s, order, s);

   compact_signature csig;
   std::memset(csig.data(), 0, csig.size());
   const int nBitsR = BN_num_bits(r);
   const int nBitsS = BN_num_bits(s);
   if (nBitsR > 256 || nBitsS > 256)
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "unable to sign");

   ECDSA_SIG_set0(sig, r, s);

   int nRecId = -1;
   for (int i = 0; i < 4; ++i) {
      auto recovered = recover_public_key_from_sig(sig, d, i, true);
      if (recovered && *recovered == pub_data) {
         nRecId = i;
         break;
      }
   }
   if (nRecId == -1)
      FORGE_THROW_EXCEPTION(exceptions::backend_error, "unable to construct recoverable key");

   csig.data()[0] = nRecId + 27 + 4;
   BN_bn2bin(r, &csig.data()[33 - (nBitsR + 7) / 8]);
   BN_bn2bin(s, &csig.data()[65 - (nBitsS + 7) / 8]);
   return csig;
}

public_key public_key::mult(const forge::crypto::sha256& digest) {
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ec_point master(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_oct2point(group, master, reinterpret_cast<const unsigned char*>(my->_key.data()),
                                 my->_key.size(), ctx));
   ssl_bignum z = bignum_from_bytes(digest.data(), digest.data_size());
   ec_point result(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_mul(group, result, nullptr, master, z, ctx));
   return public_key(point_to_public_key_data(result));
}

bool public_key::valid() const {
   return !is_empty(my->_key);
}

public_key public_key::add(const forge::crypto::sha256& digest) const {
   try {
      const ec_group& group = get_curve();
      bn_ctx ctx(BN_CTX_new());
      ssl_bignum digest_bn = bignum_from_bytes(digest.data(), digest.data_size());
      ssl_bignum order;
      EC_GROUP_get_order(group, order, ctx);
      if (BN_cmp(digest_bn, order) > 0)
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "digest is greater than group order");

      ec_point master(EC_POINT_new(group));
      FORGE_ASSERT(EC_POINT_oct2point(group, master, reinterpret_cast<const unsigned char*>(my->_key.data()),
                                    my->_key.size(), ctx));
      auto digest_key = private_key::regenerate(digest).get_public_key().serialize();
      ec_point digest_point(EC_POINT_new(group));
      FORGE_ASSERT(EC_POINT_oct2point(group, digest_point, reinterpret_cast<const unsigned char*>(digest_key.data()),
                                    digest_key.size(), ctx));
      ec_point result(EC_POINT_new(group));
      FORGE_ASSERT(EC_POINT_add(group, result, digest_point, master, ctx));
      if (EC_POINT_is_at_infinity(group, result))
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "point at infinity");
      return public_key(point_to_public_key_data(result));
   }
   FORGE_CAPTURE_AND_RETHROW("digest: ${digest}", forge::exceptions::ctx("digest", digest));
}

private_key::private_key() : my(std::make_unique<detail::private_key_impl>()) {}

private_key private_key::generate_from_seed(const forge::crypto::sha256& seed, const forge::crypto::sha256& offset) {
   ssl_bignum z = bignum_from_bytes(offset.data(), offset.data_size());
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum order;
   EC_GROUP_get_order(group, order, ctx);
   ssl_bignum secexp = bignum_from_bytes(seed.data(), seed.data_size());
   BN_add(secexp, secexp, z);
   BN_mod(secexp, secexp, order, ctx);
   forge::crypto::sha256 secret;
   FORGE_ASSERT(BN_num_bytes(secexp) <= int64_t(sizeof(secret)));
   auto shift = sizeof(secret) - BN_num_bytes(secexp);
   BN_bn2bin(secexp, reinterpret_cast<unsigned char*>(&secret) + shift);
   return regenerate(secret);
}

private_key private_key::regenerate(const forge::crypto::sha256& secret) {
   private_key self;
   self.my->_key = secret;
   FORGE_ASSERT(valid_secret(self.my->_key), "invalid P-256 private key");
   return self;
}

forge::crypto::sha256 private_key::get_secret() const {
   return my->_key;
}

private_key private_key::generate() {
   private_key self;
   do {
      forge::crypto::fill_random(std::span<std::uint8_t>{
         reinterpret_cast<std::uint8_t*>(self.my->_key.data()), self.my->_key.data_size()});
   } while (!valid_secret(self.my->_key));
   return self;
}

signature private_key::sign(const forge::crypto::sha256& digest) const {
   const auto der = sign_der(my->_key, digest);
   signature sig;
   std::memset(sig.data(), 0, sig.size());
   FORGE_ASSERT(der.size() <= sig.size(), "P-256 DER signature is too large", forge::exceptions::ctx("der_size", der.size()),
              forge::exceptions::ctx("signature_size", sig.size()));
   std::memcpy(sig.data(), der.data(), der.size());
   return sig;
}

bool public_key::verify(const forge::crypto::sha256& digest, const forge::crypto::p256::signature& sig) {
   if (is_empty(my->_key))
      return false;
   const auto der_size = der_signature_size(sig);
   if (der_size == 0 || der_size > sig.size())
      return false;
   auto key = make_public_pkey(my->_key);
   evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new(key.get(), nullptr));
   if (!ctx || 1 != EVP_PKEY_verify_init(ctx.get()))
      raise_openssl_failure("error initializing EVP P-256 verifier");
   return 1 == EVP_PKEY_verify(ctx.get(), reinterpret_cast<const unsigned char*>(sig.data()), der_size,
                               reinterpret_cast<const unsigned char*>(digest.data()), digest.data_size());
}

public_key_data public_key::serialize() const {
   return my->_key;
}

public_key::public_key() : my(std::make_unique<detail::public_key_impl>()) {}
public_key::~public_key() {}

public_key::public_key(const public_key_point_data& dat) : my(std::make_unique<detail::public_key_impl>()) {
   if (dat.data()[0] != 0)
      my->_key = normalize_public_key_data(dat.data(), dat.size());
}

public_key::public_key(const public_key_data& dat) : my(std::make_unique<detail::public_key_impl>()) {
   if (dat.data()[0] != 0)
      my->_key = normalize_public_key_data(dat.data(), dat.size());
}

bool private_key::verify(const forge::crypto::sha256& digest, const forge::crypto::p256::signature& sig) {
   return get_public_key().verify(digest, sig);
}

der_signature sign_der(const private_key_shim& key, std::span<const std::uint8_t> message) {
   const auto der = sign_der(key.serialize(), forge::crypto::sha256::hash(message));
   return der_signature{der.begin(), der.end()};
}

bool verify_der(const public_key_shim& key, std::span<const std::uint8_t> message,
                std::span<const std::uint8_t> signature_bytes) {
   if (signature_bytes.empty() || signature_bytes.size() > signature{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_signature, "invalid P-256 DER signature size");
   }
   require_der_signature(signature_bytes);
   auto value = signature{};
   std::copy(signature_bytes.begin(), signature_bytes.end(), value.begin());
   return public_key(key.serialize()).verify(forge::crypto::sha256::hash(message), value);
}

public_key private_key::get_public_key() const {
   return public_key(derive_public_key_data(my->_key));
}

forge::crypto::sha512 private_key::get_shared_secret(const public_key& other) const {
   FORGE_ASSERT(valid_secret(my->_key));
   FORGE_ASSERT(other.valid());
   const ec_group& group = get_curve();
   bn_ctx ctx(BN_CTX_new());
   ssl_bignum priv = bignum_from_bytes(my->_key.data(), my->_key.data_size());
   ec_point peer(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_oct2point(group, peer, reinterpret_cast<const unsigned char*>(other.my->_key.data()),
                                 other.my->_key.size(), ctx));
   ec_point shared(EC_POINT_new(group));
   FORGE_ASSERT(EC_POINT_mul(group, shared, nullptr, peer, priv, ctx));
   ssl_bignum x;
   FORGE_ASSERT(EC_POINT_get_affine_coordinates(group, shared, x, nullptr, ctx));
   std::array<unsigned char, 32> secret{};
   const auto bytes = BN_num_bytes(x);
   FORGE_ASSERT(bytes <= static_cast<int>(secret.size()));
   BN_bn2bin(x, secret.data() + secret.size() - bytes);
   return forge::crypto::sha512::hash(reinterpret_cast<const char*>(secret.data()), secret.size());
}

private_key::~private_key() {}

public_key::public_key(const compact_signature& c, const forge::crypto::sha256& digest, bool check_canonical)
    : my(std::make_unique<detail::public_key_impl>()) {
   my->_key = recover_public_key_data(c, digest, check_canonical);
}

compact_signature private_key::sign_compact(const forge::crypto::sha256& digest) const {
   try {
      FORGE_ASSERT(valid_secret(my->_key));
      auto my_pub_key = get_public_key().serialize();
      auto der = sign_der(my->_key, digest);
      auto sig = parse_der_signature(der);
      return signature_from_ecdsa(my_pub_key, sig, digest);
   }
   FORGE_CAPTURE_AND_RETHROW("failed to sign ${digest}", forge::exceptions::ctx("digest", digest));
}

private_key& private_key::operator=(private_key&& pk) {
   my = std::move(pk.my);
   return *this;
}

public_key::public_key(const public_key& pk)
    : my(pk.my ? std::make_unique<detail::public_key_impl>(*pk.my) : nullptr) {}

public_key::public_key(public_key&& pk) : my(std::move(pk.my)) {}

private_key::private_key(const private_key& pk)
    : my(pk.my ? std::make_unique<detail::private_key_impl>(*pk.my) : nullptr) {}

private_key::private_key(private_key&& pk) : my(std::move(pk.my)) {}

public_key& public_key::operator=(public_key&& pk) {
   my = std::move(pk.my);
   return *this;
}

public_key& public_key::operator=(const public_key& pk) {
   my = pk.my ? std::make_unique<detail::public_key_impl>(*pk.my) : nullptr;
   return *this;
}

private_key& private_key::operator=(const private_key& pk) {
   my = pk.my ? std::make_unique<detail::private_key_impl>(*pk.my) : nullptr;
   return *this;
}

} // namespace forge::crypto::p256
