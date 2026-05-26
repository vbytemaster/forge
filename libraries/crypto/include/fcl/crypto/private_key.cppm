module;
#include <variant>
#include <span>
#include <utility>
#include <boost/describe.hpp>

export module fcl.crypto.private_key;

import fcl.crypto.secp256k1;
import fcl.crypto.p256;
import fcl.crypto.ed25519;
import fcl.crypto.rsa;
import fcl.crypto.public_key;
import fcl.crypto.signature;
import fcl.crypto.sha256;
import fcl.crypto.sha512;
import fcl.reflect.reflect;
import fcl.variant.described;
import fcl.variant.static_variant;
import fcl.core.utility;
import fcl.variant;

export namespace fcl::crypto {

namespace config {
constexpr const char* private_key_base_prefix = "PVT";
constexpr const char* private_key_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};
}; // namespace config

class private_key {
 public:
   enum class algorithm {
      secp256k1,
      p256,
      ed25519,
      rsa,
   };

   using storage_type =
      std::variant<secp256k1::private_key_shim, p256::private_key_shim, ed25519::private_key_shim, rsa::private_key_shim>;

   private_key() = default;
   private_key(private_key&&) = default;
   private_key(const private_key&) = default;
   private_key& operator=(const private_key&) = default;

   [[nodiscard]] algorithm type() const noexcept;
   public_key get_public_key() const;
   signature sign(std::span<const std::uint8_t> message) const;

   template <typename T> [[nodiscard]] const T& as() const {
      return std::get<T>(_storage);
   }

   template <typename Visitor> decltype(auto) visit(Visitor&& visitor) const {
      return std::visit(std::forward<Visitor>(visitor), _storage);
   }

   template <typename KeyType = secp256k1::private_key_shim> static private_key generate() {
      return private_key(storage_type(KeyType::generate()));
   }

   template <typename KeyType = p256::private_key_shim> static private_key generate_p256() {
      return private_key(storage_type(KeyType::generate()));
   }

   template <typename KeyType = secp256k1::private_key_shim>
   static private_key regenerate(const typename KeyType::data_type& data) {
      return private_key(storage_type(KeyType(data)));
   }

   // serialize to/from string
   explicit private_key(const std::string& base58str);
   std::string to_string(const fcl::yield_function_t& yield) const;

   explicit private_key(storage_type&& other_storage) : _storage(std::move(other_storage)) {}

 private:
   storage_type _storage;
   BOOST_DESCRIBE_CLASS(private_key, (), (), (), (_storage))

   friend bool operator==(const private_key& p1, const private_key& p2);
   friend bool operator<(const private_key& p1, const private_key& p2);
}; // private_key

} // namespace fcl::crypto

export namespace fcl::crypto {
void to_variant(const crypto::private_key& var, variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());

void from_variant(const variant& var, crypto::private_key& vo);
} // namespace fcl::crypto
