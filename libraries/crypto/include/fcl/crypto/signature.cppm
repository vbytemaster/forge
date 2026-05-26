module;
#include <variant>
#include <utility>
#include <boost/describe.hpp>

export module fcl.crypto.signature;

import fcl.variant.static_variant;
import fcl.crypto.secp256k1;
import fcl.crypto.p256;
import fcl.crypto.ed25519;
import fcl.crypto.rsa;
import fcl.reflect.reflect;
import fcl.variant.described;
import fcl.core.utility;
import fcl.variant;

export namespace fcl::crypto {
namespace config {
constexpr const char* signature_base_prefix = "SIG";
constexpr const char* signature_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};
}; // namespace config

class signature {
 public:
   enum class algorithm {
      secp256k1,
      p256,
      ed25519,
      rsa,
   };

   using storage_type =
      std::variant<secp256k1::signature_shim, p256::signature_shim, ed25519::signature_shim, rsa::signature_shim>;

   signature() = default;
   signature(signature&&) = default;
   signature(const signature&) = default;
   signature& operator=(const signature&) = default;

   // serialize to/from string
   explicit signature(const std::string& base58str);
   std::string to_string(const fcl::yield_function_t& yield = fcl::yield_function_t()) const;

   [[nodiscard]] algorithm type() const noexcept;

   size_t which() const;

   size_t variable_size() const;
   const storage_type& storage() const {
      return _storage;
   }

   explicit signature(storage_type&& other_storage) : _storage(std::move(other_storage)) {}

   template <typename T> [[nodiscard]] const T& as() const {
      return std::get<T>(_storage);
   }

   template <typename Visitor> decltype(auto) visit(Visitor&& visitor) const {
      return std::visit(std::forward<Visitor>(visitor), _storage);
   }

 private:
   storage_type _storage;
   BOOST_DESCRIBE_CLASS(signature, (), (), (), (_storage))

   friend bool operator==(const signature& p1, const signature& p2);
   friend bool operator!=(const signature& p1, const signature& p2);
   friend bool operator<(const signature& p1, const signature& p2);
   friend std::size_t hash_value(const signature& b); // not cryptographic; for containers
}; // public_key

size_t hash_value(const signature& b);

} // namespace fcl::crypto

export namespace fcl::crypto {
void to_variant(const crypto::signature& var, variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());

void from_variant(const variant& var, crypto::signature& vo);
} // namespace fcl::crypto

export namespace std {
template <> struct hash<fcl::crypto::signature> {
   std::size_t operator()(const fcl::crypto::signature& k) const {
      return fcl::crypto::hash_value(k);
   }
};
} // namespace std
