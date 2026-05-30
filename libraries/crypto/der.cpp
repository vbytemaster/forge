module;

#include <fcl/exception/macros.hpp>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <type_traits>

module fcl.crypto.der;

import fcl.crypto.asymmetric;
import fcl.crypto.ed25519;
import fcl.crypto.p256;
import fcl.crypto.rsa;
import fcl.crypto.secp256k1;
import fcl.crypto.sha256;

namespace fcl::crypto::der {
using asymmetric::private_key;
using asymmetric::public_key;

namespace {

struct pkey_deleter {
   void operator()(EVP_PKEY* value) const noexcept {
      EVP_PKEY_free(value);
   }
};

using pkey_ptr = std::unique_ptr<EVP_PKEY, pkey_deleter>;

struct ec_group_deleter {
   void operator()(EC_GROUP* value) const noexcept {
      EC_GROUP_free(value);
   }
};

struct ec_point_deleter {
   void operator()(EC_POINT* value) const noexcept {
      EC_POINT_free(value);
   }
};

struct bn_ctx_deleter {
   void operator()(BN_CTX* value) const noexcept {
      BN_CTX_free(value);
   }
};

struct pkey_ctx_deleter {
   void operator()(EVP_PKEY_CTX* value) const noexcept {
      EVP_PKEY_CTX_free(value);
   }
};

using ec_group_ptr = std::unique_ptr<EC_GROUP, ec_group_deleter>;
using ec_point_ptr = std::unique_ptr<EC_POINT, ec_point_deleter>;
using bn_ctx_ptr = std::unique_ptr<BN_CTX, bn_ctx_deleter>;
using pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, pkey_ctx_deleter>;

[[noreturn]] void invalid_key(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_key, std::move(message));
}

[[noreturn]] void backend_error(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::backend_error, std::move(message));
}

[[nodiscard]] pkey_ptr parse_public(std::span<const std::uint8_t> bytes) {
   const auto* cursor = bytes.data();
   auto* raw = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(bytes.size()));
   if (raw == nullptr) {
      invalid_key("invalid public key DER");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] pkey_ptr parse_private(std::span<const std::uint8_t> bytes) {
   const auto* cursor = bytes.data();
   auto* raw = d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(bytes.size()));
   if (raw == nullptr) {
      invalid_key("invalid private key DER");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] bytes write_spki(EVP_PKEY* key) {
   const auto length = i2d_PUBKEY(key, nullptr);
   if (length <= 0) {
      backend_error("failed to size public key DER");
   }
   auto out = bytes(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key, &cursor) != length) {
      backend_error("failed to write public key DER");
   }
   return out;
}

[[nodiscard]] int ec_curve_nid(EVP_PKEY* key) {
   auto group_name = std::array<char, 64>{};
   auto written = std::size_t{};
   if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group_name.data(), group_name.size(),
                                      &written) != 1 ||
       written == 0) {
      invalid_key("EC public key does not expose a named curve");
   }
   return OBJ_sn2nid(group_name.data());
}

[[nodiscard]] bytes ec_public_octets(EVP_PKEY* key) {
   auto size = std::size_t{};
   if (EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &size) != 1 || size == 0) {
      invalid_key("failed to size EC public key point");
   }
   auto out = bytes(size);
   if (EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_PUB_KEY, out.data(), out.size(), &size) != 1) {
      invalid_key("failed to read EC public key point");
   }
   out.resize(size);
   return out;
}

template <typename Data> [[nodiscard]] Data fixed_bytes(std::span<const std::uint8_t> value, std::string_view label) {
   if (value.size() != Data{}.size()) {
      invalid_key(std::string{label} + " has unexpected size");
   }
   auto out = Data{};
   std::memcpy(out.data(), value.data(), out.size());
   return out;
}

template <typename Range> [[nodiscard]] bytes bytes_from_range(const Range& value) {
   auto out = bytes{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

[[nodiscard]] public_key read_ec_public(EVP_PKEY* key) {
   const auto point = ec_public_octets(key);
   if (point.size() != 65U) {
      invalid_key("EC public key point must be uncompressed");
   }
   const auto nid = ec_curve_nid(key);
   if (nid == NID_X9_62_prime256v1) {
      auto data = fixed_bytes<p256::public_key_point_data>(point, "P-256 public key");
      return public_key{public_key::storage_type{p256::public_key_shim{p256::public_key{data}.serialize()}}};
   }
   if (nid == NID_secp256k1) {
      auto data = fixed_bytes<secp256k1::public_key_point_data>(point, "secp256k1 public key");
      return public_key{public_key::storage_type{secp256k1::public_key_shim{secp256k1::public_key{data}.serialize()}}};
   }
   invalid_key("unsupported EC public key curve");
}

[[nodiscard]] bytes raw_public_key(EVP_PKEY* key) {
   auto size = std::size_t{};
   if (EVP_PKEY_get_raw_public_key(key, nullptr, &size) != 1 || size == 0) {
      invalid_key("failed to size raw public key");
   }
   auto out = bytes(size);
   if (EVP_PKEY_get_raw_public_key(key, out.data(), &size) != 1) {
      invalid_key("failed to read raw public key");
   }
   out.resize(size);
   return out;
}

[[nodiscard]] bytes raw_private_key(EVP_PKEY* key) {
   auto size = std::size_t{};
   if (EVP_PKEY_get_raw_private_key(key, nullptr, &size) != 1 || size == 0) {
      invalid_key("failed to size raw private key");
   }
   auto out = bytes(size);
   if (EVP_PKEY_get_raw_private_key(key, out.data(), &size) != 1) {
      invalid_key("failed to read raw private key");
   }
   out.resize(size);
   return out;
}

[[nodiscard]] bytes uncompressed_ec_point(int nid, std::span<const std::uint8_t> compressed) {
   auto group = ec_group_ptr{EC_GROUP_new_by_curve_name(nid)};
   auto context = bn_ctx_ptr{BN_CTX_new()};
   if (!group || !context) {
      backend_error("failed to allocate EC public key context");
   }
   auto point = ec_point_ptr{EC_POINT_new(group.get())};
   if (!point ||
       EC_POINT_oct2point(group.get(), point.get(), compressed.data(), compressed.size(), context.get()) != 1) {
      invalid_key("invalid EC public key point");
   }
   const auto size =
       EC_POINT_point2oct(group.get(), point.get(), POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, context.get());
   if (size == 0) {
      invalid_key("failed to size EC public key point");
   }
   auto out = bytes(size);
   if (EC_POINT_point2oct(group.get(), point.get(), POINT_CONVERSION_UNCOMPRESSED, out.data(), out.size(),
                          context.get()) != size) {
      invalid_key("failed to write EC public key point");
   }
   return out;
}

[[nodiscard]] pkey_ptr make_ec_public_key(std::string_view group_name, std::span<const std::uint8_t> compressed) {
   const auto nid = OBJ_sn2nid(std::string{group_name}.c_str());
   if (nid == NID_undef) {
      invalid_key("unsupported EC public key curve");
   }
   auto uncompressed = uncompressed_ec_point(nid, compressed);
   auto mutable_group_name = std::string{group_name};
   OSSL_PARAM params[] = {
       OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, mutable_group_name.data(), 0),
       OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, uncompressed.data(), uncompressed.size()),
       OSSL_PARAM_construct_end()};
   auto context = pkey_ctx_ptr{EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr)};
   if (!context || EVP_PKEY_fromdata_init(context.get()) != 1) {
      backend_error("failed to initialize EC public key encoder");
   }
   EVP_PKEY* raw = nullptr;
   if (EVP_PKEY_fromdata(context.get(), &raw, EVP_PKEY_PUBLIC_KEY, params) != 1 || raw == nullptr) {
      backend_error("failed to encode EC public key");
   }
   return pkey_ptr{raw};
}

[[nodiscard]] pkey_ptr make_ed25519_public_key(const ed25519::public_key_data& value) {
   auto* raw = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, value.data(), value.size());
   if (raw == nullptr) {
      invalid_key("invalid Ed25519 public key");
   }
   return pkey_ptr{raw};
}

struct bignum_deleter {
   void operator()(BIGNUM* value) const noexcept {
      BN_free(value);
   }
};

using bignum_ptr = std::unique_ptr<BIGNUM, bignum_deleter>;

[[nodiscard]] sha256 ec_private_scalar(EVP_PKEY* key) {
   BIGNUM* raw = nullptr;
   if (EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_PRIV_KEY, &raw) != 1 || raw == nullptr) {
      invalid_key("failed to read EC private key scalar");
   }
   auto scalar = bignum_ptr{raw};
   auto out = sha256{};
   if (BN_bn2binpad(scalar.get(), reinterpret_cast<unsigned char*>(out.data()), out.data_size()) !=
       static_cast<int>(out.data_size())) {
      invalid_key("EC private key scalar has unexpected size");
   }
   return out;
}

} // namespace

public_key read_public_key(std::span<const std::uint8_t> value) {
   auto key = parse_public(value);
   const auto type = EVP_PKEY_get_base_id(key.get());
   if (type == EVP_PKEY_RSA) {
      return public_key{public_key::storage_type{rsa::public_key_shim{fcl::crypto::bytes{value.begin(), value.end()}}}};
   }
   if (type == EVP_PKEY_ED25519) {
      return public_key{public_key::storage_type{
          ed25519::public_key_shim{fixed_bytes<ed25519::public_key_data>(raw_public_key(key.get()), "Ed25519 public key")}}};
   }
   if (type == EVP_PKEY_EC) {
      return read_ec_public(key.get());
   }
   invalid_key("unsupported public key type");
}

private_key read_private_key(std::span<const std::uint8_t> value) {
   auto key = parse_private(value);
   const auto type = EVP_PKEY_get_base_id(key.get());
   if (type == EVP_PKEY_RSA) {
      return private_key{private_key::storage_type{rsa::private_key_shim{fcl::crypto::bytes{value.begin(), value.end()}}}};
   }
   if (type == EVP_PKEY_ED25519) {
      return private_key{private_key::storage_type{ed25519::private_key_shim{
          fixed_bytes<ed25519::private_key_secret>(raw_private_key(key.get()), "Ed25519 private key")}}};
   }
   if (type == EVP_PKEY_EC) {
      const auto scalar = ec_private_scalar(key.get());
      const auto nid = ec_curve_nid(key.get());
      if (nid == NID_X9_62_prime256v1) {
         return private_key{private_key::storage_type{p256::private_key_shim{scalar}}};
      }
      if (nid == NID_secp256k1) {
         return private_key{private_key::storage_type{secp256k1::private_key_shim{scalar}}};
      }
      invalid_key("unsupported EC private key curve");
   }
   invalid_key("unsupported private key type");
}

bytes write_public_key(const public_key& key) {
   return key.visit([](const auto& value) -> bytes {
      using value_type = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<value_type, rsa::public_key_shim>) {
         return value.serialize();
      } else if constexpr (std::is_same_v<value_type, ed25519::public_key_shim>) {
         return write_spki(make_ed25519_public_key(value.serialize()).get());
      } else if constexpr (std::is_same_v<value_type, p256::public_key_shim>) {
         const auto data = bytes_from_range(value.serialize());
         return write_spki(make_ec_public_key("prime256v1", data).get());
      } else if constexpr (std::is_same_v<value_type, secp256k1::public_key_shim>) {
         const auto data = bytes_from_range(value.serialize());
         return write_spki(make_ec_public_key("secp256k1", data).get());
      } else {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "public key DER export is not implemented");
      }
   });
}

} // namespace fcl::crypto::der
