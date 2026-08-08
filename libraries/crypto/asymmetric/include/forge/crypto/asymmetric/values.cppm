module;

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module forge.crypto.asymmetric.values;

import forge.raw.codec;

namespace forge::crypto::asymmetric {

template <std::size_t Size>
[[nodiscard]] constexpr std::strong_ordering unsigned_byte_order(const std::array<char, Size>& left,
                                                                 const std::array<char, Size>& right) noexcept {
   return std::lexicographical_compare_three_way(
       left.begin(), left.end(), right.begin(), right.end(), [](char left_byte, char right_byte) {
          return static_cast<unsigned char>(left_byte) <=> static_cast<unsigned char>(right_byte);
       });
}

} // namespace forge::crypto::asymmetric

export namespace forge::crypto::asymmetric {

enum class algorithm : std::int32_t {
   secp256k1 = 0,
   p256 = 1,
   webauthn = 2,
   ed25519 = 3,
   rsa = 4,
};

using ecc_public_key = std::array<char, 33>;
using ecc_signature = std::array<char, 65>;

struct k1_public_key {
   using data_type = ecc_public_key;

   data_type data{};

   k1_public_key() = default;
   explicit k1_public_key(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   [[nodiscard]] constexpr std::size_t size() const noexcept {
      return data.size();
   }

   constexpr char& operator[](std::size_t position) noexcept {
      return data[position];
   }

   constexpr const char& operator[](std::size_t position) const noexcept {
      return data[position];
   }

   bool operator==(const k1_public_key&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const k1_public_key& other) const noexcept {
      return unsigned_byte_order(data, other.data);
   }
};

struct r1_public_key {
   using data_type = ecc_public_key;

   data_type data{};

   r1_public_key() = default;
   explicit r1_public_key(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   [[nodiscard]] constexpr std::size_t size() const noexcept {
      return data.size();
   }

   constexpr char& operator[](std::size_t position) noexcept {
      return data[position];
   }

   constexpr const char& operator[](std::size_t position) const noexcept {
      return data[position];
   }

   bool operator==(const r1_public_key&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const r1_public_key& other) const noexcept {
      return unsigned_byte_order(data, other.data);
   }
};

struct webauthn_public_key {
   enum class user_presence_t : std::uint8_t {
      USER_PRESENCE_NONE,
      USER_PRESENCE_PRESENT,
      USER_PRESENCE_VERIFIED,
   };

   using data_type = webauthn_public_key;

   ecc_public_key key{};
   user_presence_t user_presence = user_presence_t::USER_PRESENCE_NONE;
   std::string rpid;

   webauthn_public_key() = default;
   webauthn_public_key(ecc_public_key key_value, user_presence_t presence, std::string relying_party)
       : key(std::move(key_value)), user_presence(presence), rpid(std::move(relying_party)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return *this;
   }

   bool operator==(const webauthn_public_key&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const webauthn_public_key& other) const noexcept {
      if (const auto order = unsigned_byte_order(key, other.key); order != 0) {
         return order;
      }
      if (const auto order = user_presence <=> other.user_presence; order != 0) {
         return order;
      }
      return rpid <=> other.rpid;
   }
};

struct ed25519_public_key {
   using data_type = std::array<std::uint8_t, 32>;

   data_type data{};

   ed25519_public_key() = default;
   explicit ed25519_public_key(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const ed25519_public_key&) const = default;
   auto operator<=>(const ed25519_public_key&) const = default;
};

struct rsa_public_key {
   using data_type = std::vector<std::uint8_t>;

   data_type data;

   rsa_public_key() = default;
   explicit rsa_public_key(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const rsa_public_key&) const = default;
   auto operator<=>(const rsa_public_key&) const = default;
};

struct k1_signature {
   using data_type = ecc_signature;

   data_type data{};

   k1_signature() = default;
   explicit k1_signature(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const k1_signature&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const k1_signature& other) const noexcept {
      return unsigned_byte_order(data, other.data);
   }
};

struct r1_signature {
   using data_type = ecc_signature;

   data_type data{};

   r1_signature() = default;
   explicit r1_signature(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const r1_signature&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const r1_signature& other) const noexcept {
      return unsigned_byte_order(data, other.data);
   }
};

struct webauthn_signature {
   using data_type = webauthn_signature;

   ecc_signature compact_signature{};
   std::vector<std::uint8_t> auth_data;
   std::string client_json;

   webauthn_signature() = default;
   webauthn_signature(ecc_signature signature_value, std::vector<std::uint8_t> authenticator_data,
                      std::string client_data)
       : compact_signature(std::move(signature_value)), auth_data(std::move(authenticator_data)),
         client_json(std::move(client_data)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return *this;
   }

   bool operator==(const webauthn_signature&) const = default;
   [[nodiscard]] constexpr std::strong_ordering operator<=>(const webauthn_signature& other) const noexcept {
      if (const auto order = unsigned_byte_order(compact_signature, other.compact_signature); order != 0) {
         return order;
      }
      if (const auto order = auth_data <=> other.auth_data; order != 0) {
         return order;
      }
      return client_json <=> other.client_json;
   }
};

struct ed25519_signature {
   using data_type = std::array<std::uint8_t, 64>;

   data_type data{};

   ed25519_signature() = default;
   explicit ed25519_signature(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const ed25519_signature&) const = default;
   auto operator<=>(const ed25519_signature&) const = default;
};

struct rsa_signature {
   using data_type = std::vector<std::uint8_t>;

   data_type data;

   rsa_signature() = default;
   explicit rsa_signature(data_type value) : data(std::move(value)) {}

   [[nodiscard]] const data_type& serialize() const noexcept {
      return data;
   }

   bool operator==(const rsa_signature&) const = default;
   auto operator<=>(const rsa_signature&) const = default;
};

using public_key = std::variant<k1_public_key, r1_public_key, webauthn_public_key, ed25519_public_key, rsa_public_key>;
using signature = std::variant<k1_signature, r1_signature, webauthn_signature, ed25519_signature, rsa_signature>;

constexpr void swap(public_key& left, public_key& right) noexcept(noexcept(left.swap(right))) {
   left.swap(right);
}

constexpr void swap(signature& left, signature& right) noexcept(noexcept(left.swap(right))) {
   left.swap(right);
}

[[nodiscard]] constexpr bool operator==(const public_key& left, const public_key& right) noexcept {
   if (left.index() != right.index()) {
      return false;
   }

   return std::visit([&right]<typename Value>(const Value& value) { return value == std::get<Value>(right); }, left);
}

[[nodiscard]] constexpr std::strong_ordering operator<=>(const public_key& left, const public_key& right) noexcept {
   if (const auto order = left.index() <=> right.index(); order != 0) {
      return order;
   }

   return std::visit([&right]<typename Value>(
                         const Value& value) -> std::strong_ordering { return value <=> std::get<Value>(right); },
                     left);
}

[[nodiscard]] constexpr bool operator==(const signature& left, const signature& right) noexcept {
   if (left.index() != right.index()) {
      return false;
   }

   return std::visit([&right]<typename Value>(const Value& value) { return value == std::get<Value>(right); }, left);
}

[[nodiscard]] constexpr std::strong_ordering operator<=>(const signature& left, const signature& right) noexcept {
   if (const auto order = left.index() <=> right.index(); order != 0) {
      return order;
   }

   return std::visit([&right]<typename Value>(
                         const Value& value) -> std::strong_ordering { return value <=> std::get<Value>(right); },
                     left);
}

[[nodiscard]] constexpr algorithm type(const public_key& value) noexcept {
   return std::visit(
       []<typename Value>(const Value&) {
          if constexpr (std::same_as<Value, k1_public_key>) {
             return algorithm::secp256k1;
          } else if constexpr (std::same_as<Value, r1_public_key>) {
             return algorithm::p256;
          } else if constexpr (std::same_as<Value, webauthn_public_key>) {
             return algorithm::webauthn;
          } else if constexpr (std::same_as<Value, ed25519_public_key>) {
             return algorithm::ed25519;
          } else {
             return algorithm::rsa;
          }
       },
       value);
}

[[nodiscard]] constexpr algorithm type(const signature& value) noexcept {
   return std::visit(
       []<typename Value>(const Value&) {
          if constexpr (std::same_as<Value, k1_signature>) {
             return algorithm::secp256k1;
          } else if constexpr (std::same_as<Value, r1_signature>) {
             return algorithm::p256;
          } else if constexpr (std::same_as<Value, webauthn_signature>) {
             return algorithm::webauthn;
          } else if constexpr (std::same_as<Value, ed25519_signature>) {
             return algorithm::ed25519;
          } else {
             return algorithm::rsa;
          }
       },
       value);
}

[[nodiscard]] constexpr std::size_t index(const public_key& value) noexcept {
   return value.index();
}

[[nodiscard]] constexpr std::size_t index(const signature& value) noexcept {
   return value.index();
}

[[nodiscard]] inline std::size_t variable_size(const signature& value) noexcept {
   return std::visit(
       []<typename Value>(const Value& item) {
          if constexpr (std::same_as<Value, webauthn_signature>) {
             return item.compact_signature.size() + item.auth_data.size() + item.client_json.size();
          } else {
             return item.data.size();
          }
       },
       value);
}

template <typename Stream> void raw_pack(Stream& stream, const k1_public_key& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, k1_public_key& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const r1_public_key& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, r1_public_key& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const ed25519_public_key& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, ed25519_public_key& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const rsa_public_key& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, rsa_public_key& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const k1_signature& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, k1_signature& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const r1_signature& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, r1_signature& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const ed25519_signature& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, ed25519_signature& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const rsa_signature& value) {
   forge::raw::pack(stream, value.data);
}

template <typename Stream> void raw_unpack(Stream& stream, rsa_signature& value) {
   forge::raw::unpack(stream, value.data);
}

template <typename Stream> void raw_pack(Stream& stream, const webauthn_public_key& value) {
   forge::raw::pack(stream, value.key);
   forge::raw::pack(stream, static_cast<std::uint8_t>(value.user_presence));
   forge::raw::pack(stream, value.rpid);
}

template <typename Stream> void raw_unpack(Stream& stream, webauthn_public_key& value) {
   auto presence = std::uint8_t{};
   forge::raw::unpack(stream, value.key);
   forge::raw::unpack(stream, presence);
   value.user_presence = static_cast<webauthn_public_key::user_presence_t>(presence);
   forge::raw::unpack(stream, value.rpid);
}

template <typename Stream> void raw_pack(Stream& stream, const webauthn_signature& value) {
   forge::raw::pack(stream, value.compact_signature);
   forge::raw::pack(stream, value.auth_data);
   forge::raw::pack(stream, value.client_json);
}

template <typename Stream> void raw_unpack(Stream& stream, webauthn_signature& value) {
   forge::raw::unpack(stream, value.compact_signature);
   forge::raw::unpack(stream, value.auth_data);
   forge::raw::unpack(stream, value.client_json);
}

} // namespace forge::crypto::asymmetric
