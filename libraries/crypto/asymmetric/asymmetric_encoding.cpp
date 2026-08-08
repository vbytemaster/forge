module;
#include <forge/exceptions/macros.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

module forge.crypto.asymmetric;

import forge.codec.base58;
import forge.codec.hex;
import forge.crypto.asymmetric.ed25519;
import forge.crypto.asymmetric.p256;
import forge.crypto.digest.ripemd160;
import forge.crypto.asymmetric.rsa;
import forge.crypto.asymmetric.secp256k1;
import forge.crypto.digest.sha256;
import forge.exceptions;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::crypto::asymmetric {
namespace {

template <typename Data> [[nodiscard]] std::vector<std::uint8_t> serialize_bytes(const Data& value) {
   const auto serialized = [&]() {
      if constexpr (requires { value.get_secret(); }) {
         return value.get_secret();
      } else {
         return value.serialize();
      }
   }();
   return raw::pack(serialized);
}

template <typename Data> [[nodiscard]] Data make_value_from_bytes(const std::vector<std::uint8_t>& bytes) {
   using data_type = typename Data::data_type;

   auto unpacker = forge::datastream<const std::uint8_t*>(bytes.data(), bytes.size());
   auto data = data_type{};
   forge::raw::unpack(unpacker, data);
   if (unpacker.remaining()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   return Data{data};
}

template <typename Data> [[nodiscard]] Data make_fixed_value_from_bytes(const std::vector<std::uint8_t>& bytes) {
   using data_type = typename Data::data_type;
   if (bytes.size() != sizeof(data_type)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   return make_value_from_bytes<Data>(bytes);
}

template <typename Data> [[nodiscard]] Data make_fixed_data_from_bytes(const std::vector<std::uint8_t>& bytes) {
   if (bytes.size() != sizeof(Data)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   auto unpacker = forge::datastream<const std::uint8_t*>(bytes.data(), bytes.size());
   auto data = Data{};
   forge::raw::unpack(unpacker, data);
   if (unpacker.remaining()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "decoded key data length is invalid");
   }
   return data;
}

[[nodiscard]] std::string encode_payload(const std::vector<std::uint8_t>& payload, text_codec codec) {
   if (codec == text_codec::base58) {
      return forge::codec::base58::encode(payload);
   }
   return forge::codec::hex::encode(payload);
}

[[nodiscard]] std::vector<std::uint8_t> decode_payload(std::string_view payload, text_codec codec) {
   try {
      if (codec == text_codec::base58) {
         return forge::codec::base58::decode(payload);
      }
      return forge::codec::hex::decode(payload);
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key payload is invalid");
   }
}

[[nodiscard]] std::uint32_t first_four_bytes(const digest::sha256& digest) {
   auto result = std::uint32_t{};
   std::memcpy(&result, digest.data(), sizeof(result));
   return result;
}

[[nodiscard]] std::uint32_t calculate_rule_checksum(const std::vector<std::uint8_t>& raw_payload,
                                                    const std::vector<std::uint8_t>& encoded_payload,
                                                    const checksum_options& options) {
   if (options.scheme == checksum_scheme::none) {
      return 0;
   }

   const auto& checksum_payload = options.payload == checksum_payload::encoded_payload ? encoded_payload : raw_payload;
   if (options.scheme == checksum_scheme::single_sha256) {
      auto digest = digest::sha256::hash(std::span<const std::uint8_t>{checksum_payload});
      return first_four_bytes(digest);
   }
   if (options.scheme == checksum_scheme::double_sha256) {
      auto digest = digest::sha256::hash(std::span<const std::uint8_t>{checksum_payload});
      digest = digest::sha256::hash(digest);
      return first_four_bytes(digest);
   }

   auto encoder = digest::ripemd160::encoder{};
   encoder.write(reinterpret_cast<const char*>(checksum_payload.data()), checksum_payload.size());
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
   auto unpacker = forge::datastream<const std::uint8_t*>(payload.data(), payload.size());
   auto result = std::uint32_t{};
   forge::raw::unpack(unpacker, result);
   if (unpacker.remaining()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded checksum has invalid length");
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
            FORGE_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile parse rule is duplicated",
                                  forge::exceptions::ctx("field", std::string{field}),
                                  forge::exceptions::ctx("prefix", first->text_prefix));
         }
      }
   }
}

void validate_profile(const text_encoding_profile& profile) {
   if (profile.id.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile id is required");
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
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, "encoding profile does not support this algorithm");
}

[[nodiscard]] std::vector<std::uint8_t> decode_rule_payload(const text_encoding_rule& rule, std::string_view text) {
   if (!text.starts_with(rule.text_prefix)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key prefix is not supported by this profile");
   }

   auto payload = decode_payload(text.substr(rule.text_prefix.size()), rule.codec);
   if (payload.size() < rule.binary_prefix.size() + rule.binary_suffix.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key payload is too short");
   }
   if (!std::ranges::equal(rule.binary_prefix, payload | std::views::take(rule.binary_prefix.size()))) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key binary prefix is invalid");
   }

   auto payload_without_check = payload;
   auto actual_checksum = std::uint32_t{};
   if (rule.checksum.scheme != checksum_scheme::none) {
      if (payload_without_check.size() < 4U) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key checksum is missing");
      }
      actual_checksum = read_checksum(std::span<const std::uint8_t>{payload_without_check}.last(4U));
      payload_without_check.resize(payload_without_check.size() - 4U);
   }
   if (payload_without_check.size() < rule.binary_prefix.size() + rule.binary_suffix.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key payload is too short");
   }
   if (!std::ranges::equal(rule.binary_suffix, payload_without_check | std::views::drop(payload_without_check.size() -
                                                                                        rule.binary_suffix.size()))) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key binary suffix is invalid");
   }

   auto raw_payload =
       std::vector<std::uint8_t>(payload_without_check.begin() + static_cast<std::ptrdiff_t>(rule.binary_prefix.size()),
                                 payload_without_check.end() - static_cast<std::ptrdiff_t>(rule.binary_suffix.size()));
   if (rule.checksum.scheme != checksum_scheme::none) {
      const auto expected_checksum = calculate_rule_checksum(raw_payload, payload_without_check, rule.checksum);
      if (actual_checksum != expected_checksum) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded key checksum mismatch");
      }
   }
   return raw_payload;
}

template <typename Data>
[[nodiscard]] std::string format_rule_payload(const text_encoding_rule& rule, const Data& value) {
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
      return private_key{private_key::storage_type{
          secp256k1::private_key::regenerate(make_fixed_data_from_bytes<secp256k1::private_key_secret>(payload))}};
   case algorithm::p256:
      return private_key{private_key::storage_type{
          p256::private_key::regenerate(make_fixed_data_from_bytes<p256::private_key_secret>(payload))}};
   case algorithm::webauthn:
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "WebAuthn does not define a private key encoding");
   case algorithm::ed25519:
      return private_key{private_key::storage_type{
          ed25519::private_key::regenerate(make_fixed_data_from_bytes<ed25519::private_key_secret>(payload))}};
   case algorithm::rsa:
      return private_key{private_key::storage_type{make_value_from_bytes<rsa::private_key>(payload)}};
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded private key suite is not supported by this profile");
}

[[nodiscard]] public_key parse_public_rule(const text_encoding_rule& rule, std::string_view text) {
   const auto payload = decode_rule_payload(rule, text);
   switch (rule.type) {
   case algorithm::secp256k1:
      return public_key{make_fixed_value_from_bytes<k1_public_key>(payload)};
   case algorithm::p256:
      return public_key{make_fixed_value_from_bytes<r1_public_key>(payload)};
   case algorithm::webauthn: {
      auto value = make_value_from_bytes<webauthn_public_key>(payload);
      if (value.rpid.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "webauthn public key must have a non-empty rpid");
      }
      return public_key{std::move(value)};
   }
   case algorithm::ed25519:
      return public_key{make_fixed_value_from_bytes<ed25519_public_key>(payload)};
   case algorithm::rsa:
      return public_key{make_value_from_bytes<rsa_public_key>(payload)};
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded public key suite is not supported by this profile");
}

[[nodiscard]] signature parse_signature_rule(const text_encoding_rule& rule, std::string_view text) {
   const auto payload = decode_rule_payload(rule, text);
   switch (rule.type) {
   case algorithm::secp256k1:
      return signature{make_fixed_value_from_bytes<k1_signature>(payload)};
   case algorithm::p256:
      return signature{make_fixed_value_from_bytes<r1_signature>(payload)};
   case algorithm::webauthn:
      return signature{make_value_from_bytes<webauthn_signature>(payload)};
   case algorithm::ed25519:
      return signature{make_fixed_value_from_bytes<ed25519_signature>(payload)};
   case algorithm::rsa:
      return signature{make_value_from_bytes<rsa_signature>(payload)};
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_key, "encoded signature suite is not supported by this profile");
}

template <typename Value>
[[nodiscard]] std::string format_profile_value(const std::vector<text_encoding_rule>& rules, const Value& value) {
   const auto value_type = [&]() {
      if constexpr (requires { value.type(); }) {
         return value.type();
      } else {
         return type(value);
      }
   }();
   const auto& rule = require_format_rule(rules, value_type);
   if constexpr (requires { value.visit([](const auto&) {}); }) {
      return value.visit([&](const auto& item) { return format_rule_payload(rule, item); });
   } else {
      return std::visit([&](const auto& item) { return format_rule_payload(rule, item); }, value);
   }
}

template <typename Value, typename Parser>
[[nodiscard]] Value parse_profile_value(const std::vector<text_encoding_rule>& rules, std::string_view text,
                                        std::string_view failure_message, Parser parser) {
   auto matched_prefix = false;
   for (const auto* rule : find_parse_rules(rules, text)) {
      matched_prefix = true;
      try {
         return parser(*rule, text);
      } catch (const exceptions::invalid_key&) {
      }
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_key, std::string{failure_message},
                         forge::exceptions::ctx("matched_prefix", matched_prefix));
}

} // namespace

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

const text_encoding_profile& forge() {
   static const auto value = text_encoding_profile{
       .id = "forge",
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
               prefixed_rule(algorithm::webauthn, "PUB_WEBAUTHN_", "WEBAUTHN"),
               prefixed_rule(algorithm::ed25519, "PUB_ED25519_", "ED25519"),
               prefixed_rule(algorithm::rsa, "PUB_RSA_", "RSA"),
           },
       .signatures =
           {
               prefixed_rule(algorithm::secp256k1, "SIG_SECP256K1_", "SECP256K1"),
               prefixed_rule(algorithm::p256, "SIG_P256_", "P256"),
               prefixed_rule(algorithm::webauthn, "SIG_WEBAUTHN_", "WEBAUTHN"),
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
               prefixed_rule(algorithm::webauthn, "PUB_WA_", "WA"),
           },
       .signatures =
           {
               prefixed_rule(algorithm::secp256k1, "SIG_K1_", "K1"),
               prefixed_rule(algorithm::p256, "SIG_R1_", "R1"),
               prefixed_rule(algorithm::webauthn, "SIG_WA_", "WA"),
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

const encoding& encoding::forge() {
   static const auto value = encoding::from_profile(profiles::forge());
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

void to_variant(const private_key& var, variant& vo, const forge::yield_function_t& yield) {
   yield();
   vo = encoding::forge().format(var);
}

void from_variant(const variant& var, private_key& vo) {
   vo = encoding::forge().parse_private(var.as_string());
}

} // namespace forge::crypto::asymmetric
