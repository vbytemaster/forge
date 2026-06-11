module;
#include <fcl/exceptions/macros.hpp>
#include <boost/describe.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

export module fcl.crypto.asymmetric;

import fcl.core.utility;
import fcl.crypto.ed25519;
import fcl.crypto.p256;
import fcl.crypto.rsa;
import fcl.crypto.secp256k1;
import fcl.crypto.sha256;
export import fcl.exceptions;
import fcl.reflect.reflect;
import fcl.variant;
import fcl.variant.described;
import fcl.variant.static_variant;

export namespace fcl::crypto::asymmetric {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_options = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.asymmetric")

using invalid_key = fcl::exceptions::coded_exception<code, code::invalid_key>;
using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;

} // namespace exceptions

enum class algorithm {
   secp256k1,
   p256,
   ed25519,
   rsa,
};

class signature;
class public_key;

class private_key {
 public:
   using storage_type =
      std::variant<secp256k1::private_key_shim, p256::private_key_shim, ed25519::private_key_shim, rsa::private_key_shim>;

   private_key() = default;
   private_key(private_key&&) = default;
   private_key(const private_key&) = default;
   private_key& operator=(const private_key&) = default;

   [[nodiscard]] algorithm type() const noexcept;
   [[nodiscard]] public_key get_public_key() const;
   [[nodiscard]] signature sign(std::span<const std::uint8_t> message) const;

   template <typename T> [[nodiscard]] const T& as() const {
      return std::get<T>(_storage);
   }

   template <typename Visitor> decltype(auto) visit(Visitor&& visitor) const {
      return std::visit(std::forward<Visitor>(visitor), _storage);
   }

   template <typename KeyType = secp256k1::private_key_shim> [[nodiscard]] static private_key generate() {
      return private_key(storage_type(KeyType::generate()));
   }

   template <typename KeyType = p256::private_key_shim> [[nodiscard]] static private_key generate_p256() {
      return private_key(storage_type(KeyType::generate()));
   }

   template <typename KeyType = secp256k1::private_key_shim>
   [[nodiscard]] static private_key regenerate(const typename KeyType::data_type& data) {
      return private_key(storage_type(KeyType(data)));
   }

   explicit private_key(const std::string& text);
   [[nodiscard]] std::string to_string(const fcl::yield_function_t& yield = fcl::yield_function_t()) const;

   explicit private_key(storage_type&& other_storage) : _storage(std::move(other_storage)) {}

 private:
   storage_type _storage;
   BOOST_DESCRIBE_CLASS(private_key, (), (), (), (_storage))

   friend bool operator==(const private_key& p1, const private_key& p2);
   friend bool operator<(const private_key& p1, const private_key& p2);
};

class public_key {
 public:
   using storage_type =
      std::variant<secp256k1::public_key_shim, p256::public_key_shim, ed25519::public_key_shim, rsa::public_key_shim>;

   public_key() = default;
   public_key(public_key&&) = default;
   public_key(const public_key&) = default;
   public_key& operator=(const public_key&) = default;

   public_key(const signature& c, const sha256& digest, bool check_canonical = true);
   explicit public_key(storage_type&& other_storage) : _storage(std::move(other_storage)) {}

   [[nodiscard]] algorithm type() const noexcept;
   [[nodiscard]] bool valid() const;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature& sig) const;
   [[nodiscard]] std::size_t which() const;

   template <typename T> [[nodiscard]] const T& as() const {
      return std::get<T>(_storage);
   }

   template <typename Visitor> decltype(auto) visit(Visitor&& visitor) const {
      return std::visit(std::forward<Visitor>(visitor), _storage);
   }

   explicit public_key(const std::string& text);
   [[nodiscard]] std::string to_string(const fcl::yield_function_t& yield = fcl::yield_function_t()) const;

   storage_type _storage;
   BOOST_DESCRIBE_CLASS(public_key, (), (), (), (_storage))

 private:
   friend std::ostream& operator<<(std::ostream& s, const public_key& k);
   friend bool operator==(const public_key& p1, const public_key& p2);
   friend bool operator!=(const public_key& p1, const public_key& p2);
   friend bool operator<(const public_key& p1, const public_key& p2);
};

class signature {
 public:
   using storage_type =
      std::variant<secp256k1::signature_shim, p256::signature_shim, ed25519::signature_shim, rsa::signature_shim>;

   signature() = default;
   signature(signature&&) = default;
   signature(const signature&) = default;
   signature& operator=(const signature&) = default;

   explicit signature(const std::string& text);
   [[nodiscard]] std::string to_string(const fcl::yield_function_t& yield = fcl::yield_function_t()) const;

   [[nodiscard]] algorithm type() const noexcept;
   [[nodiscard]] std::size_t which() const;
   [[nodiscard]] std::size_t variable_size() const;
   [[nodiscard]] const storage_type& storage() const {
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
   friend std::size_t hash_value(const signature& b);
};

[[nodiscard]] std::size_t hash_value(const signature& b);

enum class checksum_scheme {
   none,
   ripemd160,
   ripemd160_with_text_suffix,
   double_sha256,
};

enum class checksum_payload {
   raw_payload,
   encoded_payload,
};

enum class text_codec {
   base58,
   hex,
};

struct checksum_options {
   checksum_scheme scheme = checksum_scheme::none;
   checksum_payload payload = checksum_payload::raw_payload;
   std::string text_suffix;
};

struct text_encoding_rule {
   algorithm type = algorithm::secp256k1;
   std::string text_prefix;
   text_codec codec = text_codec::base58;
   std::vector<std::uint8_t> binary_prefix;
   std::vector<std::uint8_t> binary_suffix;
   checksum_options checksum;
   bool parse = true;
   bool format = true;
};

struct text_encoding_profile {
   std::string id;
   std::vector<text_encoding_rule> private_keys;
   std::vector<text_encoding_rule> public_keys;
   std::vector<text_encoding_rule> signatures;
};

namespace profiles {
[[nodiscard]] const text_encoding_profile& fcl();
[[nodiscard]] const text_encoding_profile& antelope();
[[nodiscard]] const text_encoding_profile& bitcoin();
[[nodiscard]] const text_encoding_profile& solana();
[[nodiscard]] const text_encoding_profile& tezos();
};

class encoding {
 public:
   [[nodiscard]] static const encoding& fcl();
   [[nodiscard]] static const encoding& antelope();
   [[deprecated("Use encoding::antelope()")]] [[nodiscard]] static const encoding& eos();
   [[nodiscard]] static encoding custom(text_encoding_profile profile);
   [[nodiscard]] static encoding from_profile(const text_encoding_profile& profile);

   [[nodiscard]] const std::string& id() const noexcept;
   [[nodiscard]] const text_encoding_profile& profile() const noexcept;

   [[nodiscard]] public_key parse_public(std::string_view text) const;
   [[nodiscard]] private_key parse_private(std::string_view text) const;
   [[nodiscard]] signature parse_signature(std::string_view text) const;

   [[nodiscard]] std::string format(const public_key& key) const;
   [[nodiscard]] std::string format(const private_key& key) const;
   [[nodiscard]] std::string format(const signature& sig) const;

 private:
   explicit encoding(text_encoding_profile profile);

   text_encoding_profile profile_;
};

void to_variant(const private_key& var, fcl::variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());
void from_variant(const fcl::variant& var, private_key& vo);

void to_variant(const public_key& var, fcl::variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());
void from_variant(const fcl::variant& var, public_key& vo);

void to_variant(const signature& var, fcl::variant& vo,
                const fcl::yield_function_t& yield = fcl::yield_function_t());
void from_variant(const fcl::variant& var, signature& vo);

} // namespace fcl::crypto::asymmetric

export namespace std {
template <> struct hash<fcl::crypto::asymmetric::signature> {
   std::size_t operator()(const fcl::crypto::asymmetric::signature& k) const {
      return fcl::crypto::asymmetric::hash_value(k);
   }
};
} // namespace std
