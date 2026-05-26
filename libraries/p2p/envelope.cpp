module;

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

module fcl.p2p.envelope;

import fcl.crypto.der;
import fcl.crypto.ed25519;
import fcl.crypto.public_key;
import fcl.crypto.rsa;
import fcl.crypto.signature;
import fcl.multiformats;
import fcl.p2p.exceptions;

#include "protobuf.hpp"

namespace fcl::p2p {
namespace {

template <typename Range> [[nodiscard]] std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

[[nodiscard]] fcl::crypto::public_key crypto_public_key(const public_key& key) {
   if (key.data.empty()) {
      exceptions::raise(exceptions::code::invalid_identity, "signed envelope public key is empty");
   }

   if (key.type == decltype(key.type)::ed25519) {
      if (key.data.size() != fcl::crypto::ed25519::public_key_data{}.size()) {
         exceptions::raise(exceptions::code::invalid_identity, "invalid signed envelope Ed25519 key size");
      }
      auto data = fcl::crypto::ed25519::public_key_data{};
      std::copy(key.data.begin(), key.data.end(), data.begin());
      return fcl::crypto::public_key{
          fcl::crypto::public_key::storage_type{fcl::crypto::ed25519::public_key_shim{data}}};
   }

   if (key.type == decltype(key.type)::rsa) {
      return fcl::crypto::public_key{
          fcl::crypto::public_key::storage_type{fcl::crypto::rsa::public_key_shim{key.data}}};
   }

   try {
      return fcl::crypto::der::read_public_key(key.data);
   } catch (const fcl::exception::base& error) {
      exceptions::raise(exceptions::code::invalid_identity, error.what());
   }
}

[[nodiscard]] std::vector<std::uint8_t> sign(const fcl::crypto::private_key& key,
                                             std::span<const std::uint8_t> message) {
   try {
      const auto signature = key.sign(message);
      return signature.visit([](const auto& value) { return bytes_from_range(value.serialize()); });
   } catch (const fcl::exception::base& error) {
      exceptions::raise(exceptions::code::invalid_identity, error.what());
   }
}

[[nodiscard]] bool verify_signature(const public_key& key, std::span<const std::uint8_t> message,
                                    std::span<const std::uint8_t> signature) {
   if (key.type == decltype(key.type)::ed25519) {
      if (signature.size() != fcl::crypto::ed25519::signature_data{}.size()) {
         return false;
      }
      auto value = fcl::crypto::ed25519::signature_data{};
      std::copy(signature.begin(), signature.end(), value.begin());
      return crypto_public_key(key).as<fcl::crypto::ed25519::public_key_shim>().verify(message, value);
   }
   if (key.type == decltype(key.type)::rsa) {
      return fcl::crypto::rsa::public_key{key.data}.verify(message, {signature.begin(), signature.end()});
   }
   exceptions::raise(exceptions::code::invalid_identity, "ECDSA signed envelope verification requires DER signature support");
}

} // namespace

peer_id signed_envelope::signer() const {
   return make_peer_id(key);
}

std::vector<std::uint8_t> signed_envelope::signing_payload(std::string_view domain) const {
   if (domain.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "signed envelope domain must not be empty");
   }
   auto out = std::vector<std::uint8_t>{};
   auto append_len_bytes = [&out](std::span<const std::uint8_t> bytes) {
      auto length = fcl::multiformats::varint_encode(bytes.size());
      out.insert(out.end(), length.begin(), length.end());
      out.insert(out.end(), bytes.begin(), bytes.end());
   };
   const auto domain_bytes = std::vector<std::uint8_t>{domain.begin(), domain.end()};
   append_len_bytes(domain_bytes);
   append_len_bytes(payload_type);
   append_len_bytes(payload);
   return out;
}

std::vector<std::uint8_t> signed_envelope::encode() const {
   if (signature.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "signed envelope signature is empty");
   }
   auto out = std::vector<std::uint8_t>{};
   const auto encoded_key = encode_public_key(key);
   detail::append_bytes(out, 1, encoded_key);
   detail::append_bytes(out, 2, payload_type);
   detail::append_bytes(out, 3, payload);
   detail::append_bytes(out, 5, signature);
   return out;
}

void signed_envelope::verify(std::string_view domain, std::optional<peer_id> expected_signer) const {
   if (payload_type.empty() || payload.empty() || signature.empty()) {
      exceptions::raise(exceptions::code::codec_error, "signed envelope is incomplete");
   }
   const auto actual_signer = signer();
   if (expected_signer && actual_signer != *expected_signer) {
      exceptions::raise(exceptions::code::invalid_identity, "signed envelope signer peer id mismatch");
   }
   const auto message = signing_payload(domain);
   if (!verify_signature(key, message, signature)) {
      exceptions::raise(exceptions::code::invalid_identity, "signed envelope signature verification failed");
   }
}

signed_envelope signed_envelope::decode(std::span<const std::uint8_t> bytes) {
   auto out = signed_envelope{};
   auto saw_key = false;
   auto saw_payload_type = false;
   auto saw_payload = false;
   auto saw_signature = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "signed envelope public key must be bytes");
         }
         out.key = decode_public_key(in.bytes());
         saw_key = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "signed envelope payload type must be bytes");
         }
         out.payload_type = in.bytes();
         saw_payload_type = true;
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "signed envelope payload must be bytes");
         }
         out.payload = in.bytes();
         saw_payload = true;
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "signed envelope signature must be bytes");
         }
         out.signature = in.bytes();
         saw_signature = true;
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_key || !saw_payload_type || !saw_payload || !saw_signature) {
      exceptions::raise(exceptions::code::codec_error, "signed envelope missing required fields");
   }
   return out;
}

signed_envelope signed_envelope::seal(const public_key& key, const fcl::crypto::private_key& private_key,
                                      std::string_view domain,
                                      std::span<const std::uint8_t> payload_type,
                                      std::span<const std::uint8_t> payload) {
   if (payload_type.empty() || payload.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "signed envelope payload and type must not be empty");
   }
   auto out = signed_envelope{
       .key = key,
       .payload_type = std::vector<std::uint8_t>{payload_type.begin(), payload_type.end()},
       .payload = std::vector<std::uint8_t>{payload.begin(), payload.end()},
   };
   const auto message = out.signing_payload(domain);
   out.signature = sign(private_key, message);
   return out;
}

} // namespace fcl::p2p
