module;

#include <fcl/exception/macros.hpp>

#include <cstring>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

module fcl.crypto.encoding;

import fcl.core.utility;
import fcl.crypto.base58;
import fcl.crypto.common;
import fcl.crypto.exceptions;
import fcl.crypto.p256;
import fcl.crypto.secp256k1;
import fcl.crypto.sha256;
import fcl.exception.exception;
import fcl.raw.datastream;
import fcl.raw.raw;
import fcl.variant;

namespace fcl::crypto {
namespace {

template <typename Shim> [[nodiscard]] Shim parse_checked(std::string_view data, const char* checksum_prefix) {
   using data_type = typename Shim::data_type;
   using wrapper = checksummed_data<data_type>;

   const auto decoded = fcl::crypto::from_base58(std::string(data));
   auto unpacker = fcl::datastream<const char*>(decoded.data(), decoded.size());
   auto wrapped = wrapper{};
   fcl::raw::unpack(unpacker, wrapped);
   FCL_ASSERT(!unpacker.remaining(), "decoded key data length too long");
   FCL_ASSERT(wrapper::calculate_checksum(wrapped.data, checksum_prefix) == wrapped.check);
   return Shim{wrapped.data};
}

template <typename Shim> [[nodiscard]] std::string format_checked(const Shim& value, const char* checksum_prefix,
                                                                  const fcl::yield_function_t& yield = {}) {
   using data_type = typename Shim::data_type;
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

} // namespace

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
   exceptions::raise(exceptions::code::invalid_key, "unsupported EOS public key suite");
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
   exceptions::raise(exceptions::code::invalid_key, "unsupported EOS private key suite");
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
   exceptions::raise(exceptions::code::invalid_key, "unsupported EOS signature suite");
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
            exceptions::raise(exceptions::code::invalid_key, "EOS encoding does not support this public key type");
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
         exceptions::raise(exceptions::code::invalid_key, "EOS encoding does not support this private key type");
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
         exceptions::raise(exceptions::code::invalid_key, "EOS encoding does not support this signature type");
      }
   });
}

} // namespace fcl::crypto
