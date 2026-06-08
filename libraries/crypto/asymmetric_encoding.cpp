module;
#include <fcl/exceptions/macros.hpp>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

module fcl.crypto.asymmetric;

import fcl.core.utility;
import fcl.crypto.base58;
import fcl.crypto.p256;
import fcl.crypto.ripemd160;
import fcl.crypto.secp256k1;
import fcl.crypto.sha256;
import fcl.exceptions;
import fcl.raw.datastream;
import fcl.raw.raw;
import fcl.variant;

namespace fcl::crypto::asymmetric {
namespace {

constexpr const char* private_key_base_prefix = "PVT";
constexpr const char* private_key_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};
constexpr const char* public_key_base_prefix = "PUB";
constexpr const char* public_key_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};
constexpr const char* signature_base_prefix = "SIG";
constexpr const char* signature_prefix[] = {"SECP256K1", "P256", "ED25519", "RSA"};

template <typename DataType> struct checksummed_data {
   std::uint32_t check = 0;
   DataType data;

   static auto calculate_checksum(const DataType& data, const char* prefix = nullptr) {
      auto encoder = ripemd160::encoder();
      raw::pack(encoder, data);

      if (prefix != nullptr) {
         encoder.write(prefix, const_strlen(prefix));
      }
      return encoder.result()._hash[0];
   }

   template <typename Stream> friend Stream& operator<<(Stream& s, const checksummed_data& value) {
      fcl::raw::pack(s, value.data);
      fcl::raw::pack(s, value.check);
      return s;
   }

   template <typename Stream> friend Stream& operator>>(Stream& s, checksummed_data& value) {
      fcl::raw::unpack(s, value.data);
      fcl::raw::unpack(s, value.check);
      return s;
   }
};

template <typename, const char* const*, int, typename...> struct base58_str_parser_impl;

template <typename Result, const char* const* Prefixes, int Position, typename KeyType, typename... Rem>
struct base58_str_parser_impl<Result, Prefixes, Position, KeyType, Rem...> {
   static Result apply(const std::string& prefix_str, const std::string& data_str) {
      using data_type = typename KeyType::data_type;
      using wrapper = checksummed_data<data_type>;
      constexpr auto prefix = Prefixes[Position];

      if (prefix == prefix_str) {
         auto bin = fcl::crypto::from_base58(data_str);
         fcl::datastream<const char*> unpacker(bin.data(), bin.size());
         auto wrapped = wrapper{};
         fcl::raw::unpack(unpacker, wrapped);
         FCL_ASSERT(!unpacker.remaining(), "decoded base58 length too long");
         FCL_ASSERT(wrapper::calculate_checksum(wrapped.data, prefix) == wrapped.check);
         return Result(KeyType(wrapped.data));
      }

      return base58_str_parser_impl<Result, Prefixes, Position + 1, Rem...>::apply(prefix_str, data_str);
   }
};

template <typename Result, const char* const* Prefixes, int Position>
struct base58_str_parser_impl<Result, Prefixes, Position> {
   static Result apply(const std::string& prefix_str, const std::string& data_str) {
      FCL_ASSERT(false, "No matching suite type", fcl::exceptions::ctx("prefix", prefix_str),
                 fcl::exceptions::ctx("data", data_str));
   }
};

template <typename, const char* const* Prefixes> struct base58_str_parser;

template <const char* const* Prefixes, typename... Ts> struct base58_str_parser<std::variant<Ts...>, Prefixes> {
   static std::variant<Ts...> apply(const std::string& base58str) {
      const auto pivot = base58str.find('_');
      FCL_ASSERT(pivot != std::string::npos, "No delimiter in data, cannot determine suite type: ${str}",
                 fcl::exceptions::ctx("str", base58str));

      const auto prefix_str = base58str.substr(0, pivot);
      auto data_str = base58str.substr(pivot + 1);
      FCL_ASSERT(!data_str.empty(), "Data only has suite type prefix: ${str}", fcl::exceptions::ctx("str", base58str));

      return base58_str_parser_impl<std::variant<Ts...>, Prefixes, 0, Ts...>::apply(prefix_str, data_str);
   }
};

template <typename Storage, const char* const* Prefixes, int DefaultPosition = -1>
struct base58str_visitor : public fcl::visitor<std::string> {
   explicit base58str_visitor(const fcl::yield_function_t& yield) : _yield(yield) {};
   template <typename KeyType> std::string operator()(const KeyType& key) const {
      using data_type = typename KeyType::data_type;
      constexpr int position = fcl::get_index<Storage, KeyType>();
      constexpr bool is_default = position == DefaultPosition;

      auto wrapper = checksummed_data<data_type>{};
      wrapper.data = key.serialize();
      _yield();
      wrapper.check =
         checksummed_data<data_type>::calculate_checksum(wrapper.data, !is_default ? Prefixes[position] : nullptr);
      _yield();
      auto packed = raw::pack(wrapper);
      _yield();
      auto data_str = to_base58(packed.data(), packed.size(), _yield);
      _yield();
      if (!is_default) {
         data_str = std::string(Prefixes[position]) + "_" + data_str;
      }
      _yield();

      return data_str;
   }
   const fcl::yield_function_t _yield;
};

template <typename Data> [[nodiscard]] Data parse_checked(std::string_view data, const char* checksum_prefix) {
   using data_type = typename Data::data_type;
   using wrapper = checksummed_data<data_type>;

   const auto decoded = fcl::crypto::from_base58(std::string(data));
   auto unpacker = fcl::datastream<const char*>(decoded.data(), decoded.size());
   auto wrapped = wrapper{};
   fcl::raw::unpack(unpacker, wrapped);
   FCL_ASSERT(!unpacker.remaining(), "decoded key data length too long");
   FCL_ASSERT(wrapper::calculate_checksum(wrapped.data, checksum_prefix) == wrapped.check);
   return Data{wrapped.data};
}

template <typename Data>
[[nodiscard]] std::string format_checked(const Data& value, const char* checksum_prefix,
                                         const fcl::yield_function_t& yield = {}) {
   using data_type = typename Data::data_type;
   using wrapper = checksummed_data<data_type>;

   auto wrapped = wrapper{};
   wrapped.data = value.serialize();
   wrapped.check = wrapper::calculate_checksum(wrapped.data, checksum_prefix);
   const auto packed = raw::pack(wrapped);
   return to_base58(packed.data(), packed.size(), yield);
}

template <typename Data> [[nodiscard]] std::string to_wif(const Data& secret, const fcl::yield_function_t& yield = {}) {
   const auto payload_size = sizeof(typename Data::data_type) + 1U;
   auto data = std::vector<char>(payload_size + 4U);
   data[0] = static_cast<char>(0x80);
   const auto serialized = secret.serialize();
   std::memcpy(data.data() + 1, reinterpret_cast<const char*>(&serialized), sizeof(typename Data::data_type));
   auto digest = sha256::hash(data.data(), static_cast<std::uint32_t>(payload_size));
   digest = sha256::hash(digest);
   std::memcpy(data.data() + payload_size, digest.data(), 4U);
   return to_base58(data.data(), data.size(), yield);
}

template <typename Data> [[nodiscard]] Data from_wif(std::string_view wif_key) {
   const auto decoded = from_base58(std::string(wif_key));
   FCL_ASSERT(decoded.size() >= 5U, "invalid WIF private key");
   auto key_bytes = std::vector<char>(decoded.begin() + 1, decoded.end() - 4);
   auto check = sha256::hash(decoded.data(), decoded.size() - 4);
   auto check2 = sha256::hash(check);
   FCL_ASSERT(std::memcmp(check.data(), decoded.data() + decoded.size() - 4, 4) == 0 ||
              std::memcmp(check2.data(), decoded.data() + decoded.size() - 4, 4) == 0);
   return Data(fcl::variant(key_bytes).as<typename Data::data_type>());
}

[[nodiscard]] std::pair<std::string_view, std::string_view> split_prefixed(std::string_view text,
                                                                           std::string_view base) {
   const auto pivot = text.find('_');
   FCL_ASSERT(pivot != std::string_view::npos, "missing encoding prefix");
   FCL_ASSERT(text.substr(0, pivot) == base, "unexpected encoding base prefix");
   const auto rest = text.substr(pivot + 1U);
   const auto suite_pivot = rest.find('_');
   FCL_ASSERT(suite_pivot != std::string_view::npos, "missing encoding suite prefix");
   return {rest.substr(0, suite_pivot), rest.substr(suite_pivot + 1U)};
}

template <typename Storage, const char* const* Prefixes>
[[nodiscard]] Storage parse_fcl_text(const std::string& text, const char* base_prefix) {
   const auto pivot = text.find('_');
   FCL_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine key type",
              fcl::exceptions::ctx("str", text));
   const auto prefix_str = text.substr(0, pivot);
   FCL_ASSERT(std::string_view(base_prefix) == prefix_str, "invalid key prefix", fcl::exceptions::ctx("str", text),
              fcl::exceptions::ctx("prefix_str", prefix_str));
   auto data_str = text.substr(pivot + 1);
   FCL_ASSERT(!data_str.empty(), "key has no data: ${str}", fcl::exceptions::ctx("str", text));
   return base58_str_parser<Storage, Prefixes>::apply(data_str);
}

} // namespace

private_key::private_key(const std::string& text)
    : _storage(parse_fcl_text<private_key::storage_type, private_key_prefix>(text, private_key_base_prefix)) {}

std::string private_key::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, private_key_prefix>(yield), _storage);
   return std::string(private_key_base_prefix) + "_" + data_str;
}

public_key::public_key(const std::string& text)
    : _storage(parse_fcl_text<public_key::storage_type, public_key_prefix>(text, public_key_base_prefix)) {}

std::string public_key::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, public_key_prefix>(yield), _storage);
   return std::string(public_key_base_prefix) + "_" + data_str;
}

signature::signature(const std::string& text)
    : _storage(parse_fcl_text<signature::storage_type, signature_prefix>(text, signature_base_prefix)) {}

std::string signature::to_string(const fcl::yield_function_t& yield) const {
   auto data_str = std::visit(base58str_visitor<storage_type, signature_prefix>(yield), _storage);
   yield();
   return std::string(signature_base_prefix) + "_" + data_str;
}

const encoding& encoding::fcl() {
   static constexpr auto value = encoding{kind::fcl};
   return value;
}

const encoding& encoding::eos() {
   static constexpr auto value = encoding{kind::eos};
   return value;
}

encoding::kind encoding::id() const noexcept {
   return kind_;
}

public_key encoding::parse_public(std::string_view text) const {
   if (kind_ == kind::fcl) {
      return public_key{std::string(text)};
   }
   if (text.starts_with("EOS") && text.find('_') == std::string_view::npos) {
      return public_key{public_key::storage_type{
         parse_checked<secp256k1::public_key_shim>(text.substr(3U), nullptr)}};
   }

   const auto [suite, payload] = split_prefixed(text, "PUB");
   if (suite == "K1") {
      return public_key{public_key::storage_type{parse_checked<secp256k1::public_key_shim>(payload, "K1")}};
   }
   if (suite == "R1") {
      return public_key{public_key::storage_type{parse_checked<p256::public_key_shim>(payload, "R1")}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "unsupported EOS public key suite");
}

private_key encoding::parse_private(std::string_view text) const {
   if (kind_ == kind::fcl) {
      return private_key{std::string(text)};
   }
   if (text.find('_') == std::string_view::npos) {
      return private_key{private_key::storage_type{from_wif<secp256k1::private_key_shim>(text)}};
   }
   const auto [suite, payload] = split_prefixed(text, "PVT");
   if (suite == "K1") {
      return private_key{private_key::storage_type{parse_checked<secp256k1::private_key_shim>(payload, "K1")}};
   }
   if (suite == "R1") {
      return private_key{private_key::storage_type{parse_checked<p256::private_key_shim>(payload, "R1")}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "unsupported EOS private key suite");
}

signature encoding::parse_signature(std::string_view text) const {
   if (kind_ == kind::fcl) {
      return signature{std::string(text)};
   }
   const auto [suite, payload] = split_prefixed(text, "SIG");
   if (suite == "K1") {
      return signature{signature::storage_type{parse_checked<secp256k1::signature_shim>(payload, "K1")}};
   }
   if (suite == "R1") {
      return signature{signature::storage_type{parse_checked<p256::signature_shim>(payload, "R1")}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "unsupported EOS signature suite");
}

std::string encoding::format(const public_key& key) const {
   if (kind_ == kind::fcl) {
      return key.to_string({});
   }
   return std::visit(
      [](const auto& value) -> std::string {
         using key_type = std::decay_t<decltype(value)>;
         if constexpr (std::is_same_v<key_type, secp256k1::public_key_shim>) {
            return "EOS" + format_checked(value, nullptr);
         } else if constexpr (std::is_same_v<key_type, p256::public_key_shim>) {
            return "PUB_R1_" + format_checked(value, "R1");
         } else {
            FCL_THROW_EXCEPTION(exceptions::invalid_key, "EOS encoding does not support this public key type");
         }
      },
      key._storage);
}

std::string encoding::format(const private_key& key) const {
   if (kind_ == kind::fcl) {
      return key.to_string({});
   }
   return key.visit([](const auto& value) -> std::string {
      using key_type = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<key_type, secp256k1::private_key_shim>) {
         return to_wif(value);
      } else if constexpr (std::is_same_v<key_type, p256::private_key_shim>) {
         return "PVT_R1_" + format_checked(value, "R1");
      } else {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "EOS encoding does not support this private key type");
      }
   });
}

std::string encoding::format(const signature& sig) const {
   if (kind_ == kind::fcl) {
      return sig.to_string({});
   }
   return sig.visit([](const auto& value) -> std::string {
      using sig_type = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<sig_type, secp256k1::signature_shim>) {
         return "SIG_K1_" + format_checked(value, "K1");
      } else if constexpr (std::is_same_v<sig_type, p256::signature_shim>) {
         return "SIG_R1_" + format_checked(value, "R1");
      } else {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "EOS encoding does not support this signature type");
      }
   });
}

void to_variant(const private_key& var, variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const variant& var, private_key& vo) {
   vo = private_key(var.as_string());
}

void to_variant(const public_key& var, variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const variant& var, public_key& vo) {
   vo = public_key(var.as_string());
}

void to_variant(const signature& var, variant& vo, const fcl::yield_function_t& yield) {
   vo = var.to_string(yield);
}

void from_variant(const variant& var, signature& vo) {
   vo = signature(var.as_string());
}

} // namespace fcl::crypto::asymmetric
