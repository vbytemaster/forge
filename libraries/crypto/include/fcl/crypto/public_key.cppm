module;
#include <variant>
#include <span>
#include <utility>
#include <boost/describe.hpp>

export module fcl.crypto.public_key;

import fcl.crypto.secp256k1;
import fcl.crypto.p256;
import fcl.crypto.ed25519;
import fcl.crypto.rsa;
import fcl.crypto.signature;
import fcl.reflect.reflect;
import fcl.variant.described;
import fcl.variant.static_variant;
import fcl.crypto.sha256;
import fcl.core.utility;
import fcl.variant;

export namespace fcl::crypto {
namespace config {
constexpr const char* public_key_base_prefix = "PUB";
constexpr const char* public_key_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};
}; // namespace config

class public_key {
 public:
   enum class algorithm {
      secp256k1,
      p256,
      ed25519,
      rsa,
   };

   using storage_type =
      std::variant<secp256k1::public_key_shim, p256::public_key_shim, ed25519::public_key_shim, rsa::public_key_shim>;

   public_key() = default;
   public_key(public_key&&) = default;
   public_key(const public_key&) = default;
   public_key& operator=(const public_key&) = default;

   public_key(const signature& c, const sha256& digest, bool check_canonical = true);

   public_key(storage_type&& other_storage) : _storage(std::move(other_storage)) {}

   [[nodiscard]] algorithm type() const noexcept;
   bool valid() const;
   bool verify(std::span<const std::uint8_t> message, const signature& sig) const;

   size_t which() const;

   template <typename T> [[nodiscard]] const T& as() const {
      return std::get<T>(_storage);
   }

   template <typename Visitor> decltype(auto) visit(Visitor&& visitor) const {
      return std::visit(std::forward<Visitor>(visitor), _storage);
   }

   // serialize to/from string
   explicit public_key(const std::string& base58str);
   std::string to_string(const fcl::yield_function_t& yield) const;

   storage_type _storage;
   BOOST_DESCRIBE_CLASS(public_key, (), (), (), (_storage))

 private:
   friend std::ostream& operator<<(std::ostream& s, const public_key& k);
   friend bool operator==(const public_key& p1, const public_key& p2);
   friend bool operator!=(const public_key& p1, const public_key& p2);
   friend bool operator<(const public_key& p1, const public_key& p2);
}; // public_key

} // namespace fcl::crypto

export namespace fcl::crypto {
void to_variant(const crypto::public_key& var, variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());

void from_variant(const variant& var, crypto::public_key& vo);
} // namespace fcl::crypto
