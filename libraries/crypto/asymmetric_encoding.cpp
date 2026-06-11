module;
#include <fcl/exceptions/macros.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

module fcl.crypto.asymmetric;

import fcl.core.utility;
import fcl.crypto.base58;
import fcl.crypto.ed25519;
import fcl.crypto.p256;
import fcl.crypto.ripemd160;
import fcl.crypto.rsa;
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

[[nodiscard]] std::vector<std::uint8_t> to_bytes(const std::vector<char>& input) {
   return std::vector<std::uint8_t>(input.begin(), input.end());
}

[[nodiscard]] std::vector<char> to_chars(const std::vector<std::uint8_t>& input) {
   return std::vector<char>(input.begin(), input.end());
}

template <typename Data> [[nodiscard]] std::vector<std::uint8_t> serialize_bytes(const Data& value) {
   const auto serialized = value.serialize();
   const auto packed = raw::pack(serialized);
   return to_bytes(packed);
}

template <typename Data> [[nodiscard]] Data make_value_from_bytes(const std::vector<std::uint8_t>& bytes) {
   using data_type = typename Data::data_type;

   const auto chars = to_chars(bytes);
   auto unpacker = fcl::datastream<const char*>(chars.data(), chars.size());
   auto data = data_type{};
   fcl::raw::unpack(unpacker, data);
   if (unpacker.remaining()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   return Data{data};
}

template <typename Data> [[nodiscard]] Data make_fixed_value_from_bytes(const std::vector<std::uint8_t>& bytes) {
   using data_type = typename Data::data_type;
   if (bytes.size() != sizeof(data_type)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   return make_value_from_bytes<Data>(bytes);
}

[[nodiscard]] char hex_digit(std::uint8_t value) {
   return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

[[nodiscard]] std::string encode_payload(const std::vector<std::uint8_t>& payload, text_codec codec) {
   if (codec == text_codec::base58) {
      return base58_encode(payload);
   }
   auto result = std::string{};
   result.reserve(payload.size() * 2U);
   for (const auto byte : payload) {
      result.push_back(hex_digit(static_cast<std::uint8_t>(byte >> 4U)));
      result.push_back(hex_digit(static_cast<std::uint8_t>(byte & 0x0fU)));
   }
   return result;
}

[[nodiscard]] std::uint8_t parse_hex_digit(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<std::uint8_t>(10 + value - 'a');
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<std::uint8_t>(10 + value - 'A');
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key contains invalid hex digit");
}

[[nodiscard]] std::vector<std::uint8_t> decode_payload(std::string_view payload, text_codec codec) {
   if (codec == text_codec::base58) {
      return base58_decode(payload);
   }
   if (payload.size() % 2U != 0U) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key hex payload has odd length");
   }
   auto result = std::vector<std::uint8_t>{};
   result.reserve(payload.size() / 2U);
   for (std::size_t i = 0; i < payload.size(); i += 2U) {
      result.push_back(static_cast<std::uint8_t>((parse_hex_digit(payload[i]) << 4U) | parse_hex_digit(payload[i + 1U])));
   }
   return result;
}

[[nodiscard]] std::uint32_t first_four_bytes(const sha256& digest) {
   auto result = std::uint32_t{};
   std::memcpy(&result, digest.data(), sizeof(result));
   return result;
}

[[nodiscard]] std::vector<char> as_chars(const std::vector<std::uint8_t>& value) {
   return std::vector<char>(value.begin(), value.end());
}

[[nodiscard]] std::uint32_t calculate_rule_checksum(const std::vector<std::uint8_t>& raw_payload,
                                                    const std::vector<std::uint8_t>& encoded_payload,
                                                    const checksum_options& options) {
   if (options.scheme == checksum_scheme::none) {
      return 0;
   }

   const auto& checksum_payload =
      options.payload == checksum_payload::encoded_payload ? encoded_payload : raw_payload;
   if (options.scheme == checksum_scheme::single_sha256) {
      auto chars = as_chars(checksum_payload);
      auto digest = sha256::hash(chars.data(), static_cast<std::uint32_t>(chars.size()));
      return first_four_bytes(digest);
   }
   if (options.scheme == checksum_scheme::double_sha256) {
      auto chars = as_chars(checksum_payload);
      auto digest = sha256::hash(chars.data(), static_cast<std::uint32_t>(chars.size()));
      digest = sha256::hash(digest);
      return first_four_bytes(digest);
   }

   auto encoder = ripemd160::encoder{};
   const auto chars = as_chars(checksum_payload);
   encoder.write(chars.data(), chars.size());
   if (options.scheme == checksum_scheme::ripemd160_with_text_suffix) {
      encoder.write(options.text_suffix.data(), options.text_suffix.size());
   }
   return encoder.result()._hash[0];
}

void append_checksum(std::vector<std::uint8_t>& payload, std::uint32_t checksum) {
   const auto packed = raw::pack(checksum);
   payload.insert(payload.end(), packed.begin(), packed.end());
}

[[nodiscard]] std::uint32_t read_checksum(std::span<const std::uint8_t> payload) {
   auto chars = std::vector<char>(payload.begin(), payload.end());
   auto unpacker = fcl::datastream<const char*>(chars.data(), chars.size());
   auto result = std::uint32_t{};
   fcl::raw::unpack(unpacker, result);
   if (unpacker.remaining()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded checksum has invalid length");
   }
   return result;
}

[[nodiscard]] bool same_checksum_options(const checksum_options& lhs, const checksum_options& rhs) {
   return lhs.scheme == rhs.scheme && lhs.payload == rhs.payload && lhs.text_suffix == rhs.text_suffix;
}

[[nodiscard]] bool same_parse_rule(const text_encoding_rule& lhs, const text_encoding_rule& rhs) {
   return lhs.parse == rhs.parse && lhs.type == rhs.type && lhs.text_prefix == rhs.text_prefix &&
          lhs.codec == rhs.codec && lhs.binary_prefix == rhs.binary_prefix && lhs.binary_suffix == rhs.binary_suffix &&
          same_checksum_options(lhs.checksum, rhs.checksum);
}

void validate_parse_rules(const std::vector<text_encoding_rule>& rules, std::string_view field) {
   for (auto first = rules.begin(); first != rules.end(); ++first) {
      if (!first->parse) {
         continue;
      }
      auto second = first;
      for (++second; second != rules.end(); ++second) {
         if (second->parse && same_parse_rule(*first, *second)) {
            FCL_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile parse rule is duplicated",
                                fcl::exceptions::ctx("field", std::string{field}),
                                fcl::exceptions::ctx("prefix", first->text_prefix));
         }
      }
   }
}

void validate_profile(const text_encoding_profile& profile) {
   if (profile.id.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile id is required");
   }
   validate_parse_rules(profile.private_keys, "private_keys");
   validate_parse_rules(profile.public_keys, "public_keys");
   validate_parse_rules(profile.signatures, "signatures");
}

[[nodiscard]] std::vector<const text_encoding_rule*> find_parse_rules(const std::vector<text_encoding_rule>& rules,
                                                                      std::string_view text) {
   auto result = std::vector<const text_encoding_rule*>{};
   for (const auto& rule : rules) {
      if (!rule.parse) {
         continue;
      }
      if (rule.text_prefix.empty() || text.starts_with(rule.text_prefix)) {
         result.push_back(&rule);
      }
   }
   std::ranges::stable_sort(result, [](const auto* lhs, const auto* rhs) {
      if (lhs->text_prefix.size() != rhs->text_prefix.size()) {
         return lhs->text_prefix.size() > rhs->text_prefix.size();
      }
      if (lhs->binary_prefix.size() != rhs->binary_prefix.size()) {
         return lhs->binary_prefix.size() > rhs->binary_prefix.size();
      }
      return lhs->binary_suffix.size() > rhs->binary_suffix.size();
   });
   return result;
}

[[nodiscard]] const text_encoding_rule& require_format_rule(const std::vector<text_encoding_rule>& rules,
                                                            algorithm value) {
   for (const auto& rule : rules) {
      if (rule.format && rule.type == value) {
         return rule;
      }
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile does not support this algorithm");
}

[[nodiscard]] std::vector<std::uint8_t> decode_rule_payload(const text_encoding_rule& rule, std::string_view text) {
   if (!text.starts_with(rule.text_prefix)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key prefix is not supported by this profile");
   }

   auto payload = decode_payload(text.substr(rule.text_prefix.size()), rule.codec);
   if (payload.size() < rule.binary_prefix.size() + rule.binary_suffix.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key payload is too short");
   }
   if (!std::ranges::equal(rule.binary_prefix, payload | std::views::take(rule.binary_prefix.size()))) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key binary prefix is invalid");
   }

   auto payload_without_check = payload;
   auto actual_checksum = std::uint32_t{};
   if (rule.checksum.scheme != checksum_scheme::none) {
      if (payload_without_check.size() < 4U) {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key checksum is missing");
      }
      actual_checksum = read_checksum(std::span<const std::uint8_t>{payload_without_check}.last(4U));
      payload_without_check.resize(payload_without_check.size() - 4U);
   }
   if (payload_without_check.size() < rule.binary_prefix.size() + rule.binary_suffix.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key payload is too short");
   }
   if (!std::ranges::equal(rule.binary_suffix,
                           payload_without_check |
                              std::views::drop(payload_without_check.size() - rule.binary_suffix.size()))) {
      FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key binary suffix is invalid");
   }

   auto raw_payload = std::vector<std::uint8_t>(
      payload_without_check.begin() + static_cast<std::ptrdiff_t>(rule.binary_prefix.size()),
      payload_without_check.end() - static_cast<std::ptrdiff_t>(rule.binary_suffix.size()));
   if (rule.checksum.scheme != checksum_scheme::none) {
      const auto expected_checksum =
         calculate_rule_checksum(raw_payload, payload_without_check, rule.checksum);
      if (actual_checksum != expected_checksum) {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded key checksum mismatch");
      }
   }
   return raw_payload;
}

template <typename Data> [[nodiscard]] std::string format_rule_payload(const text_encoding_rule& rule,
                                                                       const Data& value) {
   auto raw_payload = serialize_bytes(value);
   auto encoded_payload = rule.binary_prefix;
   encoded_payload.insert(encoded_payload.end(), raw_payload.begin(), raw_payload.end());
   encoded_payload.insert(encoded_payload.end(), rule.binary_suffix.begin(), rule.binary_suffix.end());
   if (rule.checksum.scheme != checksum_scheme::none) {
      append_checksum(encoded_payload, calculate_rule_checksum(raw_payload, encoded_payload, rule.checksum));
   }
   return rule.text_prefix + encode_payload(encoded_payload, rule.codec);
}

[[nodiscard]] private_key parse_private_rule(const text_encoding_rule& rule, std::string_view text) {
   const auto payload = decode_rule_payload(rule, text);
   switch (rule.type) {
   case algorithm::secp256k1:
      return private_key{private_key::storage_type{make_fixed_value_from_bytes<secp256k1::private_key_shim>(payload)}};
   case algorithm::p256:
      return private_key{private_key::storage_type{make_fixed_value_from_bytes<p256::private_key_shim>(payload)}};
   case algorithm::ed25519:
      return private_key{private_key::storage_type{make_fixed_value_from_bytes<ed25519::private_key_shim>(payload)}};
   case algorithm::rsa:
      return private_key{private_key::storage_type{make_value_from_bytes<rsa::private_key_shim>(payload)}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded private key suite is not supported by this profile");
}

[[nodiscard]] public_key parse_public_rule(const text_encoding_rule& rule, std::string_view text) {
   const auto payload = decode_rule_payload(rule, text);
   switch (rule.type) {
   case algorithm::secp256k1:
      return public_key{public_key::storage_type{make_fixed_value_from_bytes<secp256k1::public_key_shim>(payload)}};
   case algorithm::p256:
      return public_key{public_key::storage_type{make_fixed_value_from_bytes<p256::public_key_shim>(payload)}};
   case algorithm::ed25519:
      return public_key{public_key::storage_type{make_fixed_value_from_bytes<ed25519::public_key_shim>(payload)}};
   case algorithm::rsa:
      return public_key{public_key::storage_type{make_value_from_bytes<rsa::public_key_shim>(payload)}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded public key suite is not supported by this profile");
}

[[nodiscard]] signature parse_signature_rule(const text_encoding_rule& rule, std::string_view text) {
   const auto payload = decode_rule_payload(rule, text);
   switch (rule.type) {
   case algorithm::secp256k1:
      return signature{signature::storage_type{make_fixed_value_from_bytes<secp256k1::signature_shim>(payload)}};
   case algorithm::p256:
      return signature{signature::storage_type{make_fixed_value_from_bytes<p256::signature_shim>(payload)}};
   case algorithm::ed25519:
      return signature{signature::storage_type{make_fixed_value_from_bytes<ed25519::signature_shim>(payload)}};
   case algorithm::rsa:
      return signature{signature::storage_type{make_value_from_bytes<rsa::signature_shim>(payload)}};
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, "encoded signature suite is not supported by this profile");
}

template <typename Value>
[[nodiscard]] std::string format_profile_value(const std::vector<text_encoding_rule>& rules, const Value& value) {
   const auto& rule = require_format_rule(rules, value.type());
   return value.visit([&](const auto& item) {
      return format_rule_payload(rule, item);
   });
}

template <typename Value, typename Parser>
[[nodiscard]] Value parse_profile_value(const std::vector<text_encoding_rule>& rules,
                                        std::string_view text,
                                        std::string_view failure_message,
                                        Parser parser) {
   auto matched_prefix = false;
   for (const auto* rule : find_parse_rules(rules, text)) {
      matched_prefix = true;
      try {
         return parser(*rule, text);
      } catch (const exceptions::invalid_key&) {
      }
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_key, std::string{failure_message},
                       fcl::exceptions::ctx("matched_prefix", matched_prefix));
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

namespace profiles {
namespace {

[[nodiscard]] checksum_options ripemd_suffix(std::string suffix) {
   return checksum_options{
      .scheme = checksum_scheme::ripemd160_with_text_suffix,
      .payload = checksum_payload::raw_payload,
      .text_suffix = std::move(suffix),
   };
}

[[nodiscard]] checksum_options ripemd_plain() {
   return checksum_options{
      .scheme = checksum_scheme::ripemd160,
      .payload = checksum_payload::raw_payload,
   };
}

[[nodiscard]] checksum_options base58check() {
   return checksum_options{
      .scheme = checksum_scheme::double_sha256,
      .payload = checksum_payload::encoded_payload,
   };
}

[[nodiscard]] checksum_options wif_single_sha_checksum() {
   return checksum_options{
      .scheme = checksum_scheme::single_sha256,
      .payload = checksum_payload::encoded_payload,
   };
}

[[nodiscard]] text_encoding_rule prefixed_rule(algorithm type, std::string prefix, std::string checksum_suffix,
                                               bool parse = true, bool format = true) {
   return text_encoding_rule{
      .type = type,
      .text_prefix = std::move(prefix),
      .checksum = ripemd_suffix(std::move(checksum_suffix)),
      .parse = parse,
      .format = format,
   };
}

} // namespace

const text_encoding_profile& fcl() {
   static const auto value = text_encoding_profile{
      .id = "fcl",
      .private_keys =
         {
            prefixed_rule(algorithm::secp256k1, "PVT_SECP256K1_", "SECP256K1"),
            prefixed_rule(algorithm::p256, "PVT_P256_", "P256"),
            prefixed_rule(algorithm::ed25519, "PVT_ED25519_", "ED25519"),
            prefixed_rule(algorithm::rsa, "PVT_RSA_", "RSA"),
         },
      .public_keys =
         {
            prefixed_rule(algorithm::secp256k1, "PUB_SECP256K1_", "SECP256K1"),
            prefixed_rule(algorithm::p256, "PUB_P256_", "P256"),
            prefixed_rule(algorithm::ed25519, "PUB_ED25519_", "ED25519"),
            prefixed_rule(algorithm::rsa, "PUB_RSA_", "RSA"),
         },
      .signatures =
         {
            prefixed_rule(algorithm::secp256k1, "SIG_SECP256K1_", "SECP256K1"),
            prefixed_rule(algorithm::p256, "SIG_P256_", "P256"),
            prefixed_rule(algorithm::ed25519, "SIG_ED25519_", "ED25519"),
            prefixed_rule(algorithm::rsa, "SIG_RSA_", "RSA"),
         },
   };
   return value;
}

const text_encoding_profile& antelope() {
   static const auto value = text_encoding_profile{
      .id = "antelope",
      .private_keys =
         {
            text_encoding_rule{
               .type = algorithm::secp256k1,
               .text_prefix = "",
               .binary_prefix = {0x80},
               .checksum = base58check(),
            },
            text_encoding_rule{
               .type = algorithm::secp256k1,
               .text_prefix = "",
               .binary_prefix = {0x80},
               .checksum = wif_single_sha_checksum(),
               .format = false,
            },
            prefixed_rule(algorithm::secp256k1, "PVT_K1_", "K1", true, false),
            prefixed_rule(algorithm::p256, "PVT_R1_", "R1"),
         },
      .public_keys =
         {
            text_encoding_rule{
               .type = algorithm::secp256k1,
               .text_prefix = "EOS",
               .checksum = ripemd_plain(),
            },
            prefixed_rule(algorithm::secp256k1, "PUB_K1_", "K1", true, false),
            prefixed_rule(algorithm::p256, "PUB_R1_", "R1"),
         },
      .signatures =
         {
            prefixed_rule(algorithm::secp256k1, "SIG_K1_", "K1"),
            prefixed_rule(algorithm::p256, "SIG_R1_", "R1"),
         },
   };
   return value;
}

const text_encoding_profile& bitcoin() {
   static const auto value = text_encoding_profile{
      .id = "bitcoin",
      .private_keys =
         {
            text_encoding_rule{
               .type = algorithm::secp256k1,
               .binary_prefix = {0x80},
               .binary_suffix = {0x01},
               .checksum = base58check(),
            },
            text_encoding_rule{
               .type = algorithm::secp256k1,
               .binary_prefix = {0x80},
               .checksum = base58check(),
               .format = false,
            },
         },
   };
   return value;
}

const text_encoding_profile& solana() {
   static const auto value = text_encoding_profile{
      .id = "solana",
      .private_keys =
         {
            text_encoding_rule{.type = algorithm::ed25519},
         },
      .public_keys =
         {
            text_encoding_rule{.type = algorithm::ed25519},
         },
      .signatures =
         {
            text_encoding_rule{.type = algorithm::ed25519},
         },
   };
   return value;
}

const text_encoding_profile& tezos() {
   static const auto value = text_encoding_profile{
      .id = "tezos",
      .private_keys =
         {
            text_encoding_rule{
               .type = algorithm::ed25519,
               .binary_prefix = {43, 246, 78, 7},
               .checksum = base58check(),
            },
         },
      .public_keys =
         {
            text_encoding_rule{
               .type = algorithm::ed25519,
               .binary_prefix = {13, 15, 37, 217},
               .checksum = base58check(),
            },
         },
      .signatures =
         {
            text_encoding_rule{
               .type = algorithm::ed25519,
               .binary_prefix = {9, 245, 205, 134, 18},
               .checksum = base58check(),
            },
         },
   };
   return value;
}

} // namespace profiles

const encoding& encoding::fcl() {
   static const auto value = encoding::from_profile(profiles::fcl());
   return value;
}

const encoding& encoding::eos() {
   return encoding::antelope();
}

const encoding& encoding::antelope() {
   static const auto value = encoding::from_profile(profiles::antelope());
   return value;
}

encoding encoding::custom(text_encoding_profile profile) {
   validate_profile(profile);
   return encoding{std::move(profile)};
}

encoding encoding::from_profile(const text_encoding_profile& profile) {
   validate_profile(profile);
   return encoding{text_encoding_profile{profile}};
}

encoding::encoding(text_encoding_profile profile) : profile_(std::move(profile)) {}

const std::string& encoding::id() const noexcept {
   return profile_.id;
}

const text_encoding_profile& encoding::profile() const noexcept {
   return profile_;
}

public_key encoding::parse_public(std::string_view text) const {
   return parse_profile_value<public_key>(
      profile_.public_keys, text, "encoded public key prefix is not supported by this profile", parse_public_rule);
}

private_key encoding::parse_private(std::string_view text) const {
   return parse_profile_value<private_key>(
      profile_.private_keys, text, "encoded private key prefix is not supported by this profile", parse_private_rule);
}

signature encoding::parse_signature(std::string_view text) const {
   return parse_profile_value<signature>(
      profile_.signatures, text, "encoded signature prefix is not supported by this profile", parse_signature_rule);
}

std::string encoding::format(const public_key& key) const {
   return format_profile_value(profile_.public_keys, key);
}

std::string encoding::format(const private_key& key) const {
   return format_profile_value(profile_.private_keys, key);
}

std::string encoding::format(const signature& sig) const {
   return format_profile_value(profile_.signatures, sig);
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
