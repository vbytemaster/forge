module;

#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module fcl.p2p.envelope;

import fcl.crypto.asymmetric;
import fcl.multiformats.exceptions;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.multicodec;
import fcl.multiformats.multihash;
import fcl.multiformats.multibase;
import fcl.multiformats.multiaddr;
import fcl.p2p.exceptions;
import fcl.p2p.identity;

#include "identity_signature.hpp"
#include "protobuf.hpp"

namespace fcl::p2p {

peer_id signed_envelope::signer() const {
   return make_peer_id(key);
}

std::vector<std::uint8_t> signed_envelope::signing_payload(std::string_view domain) const {
   if (domain.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "signed envelope domain must not be empty");
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
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "signed envelope signature is empty");
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
      FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope is incomplete");
   }
   const auto actual_signer = signer();
   if (expected_signer && actual_signer != *expected_signer) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "signed envelope signer peer id mismatch");
   }
   const auto message = signing_payload(domain);
   if (!verify_identity_signature(key, message, signature)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "signed envelope signature verification failed");
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
            FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope public key must be bytes");
         }
         out.key = decode_public_key(in.bytes());
         saw_key = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope payload type must be bytes");
         }
         out.payload_type = in.bytes();
         saw_payload_type = true;
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope payload must be bytes");
         }
         out.payload = in.bytes();
         saw_payload = true;
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope signature must be bytes");
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
      FCL_THROW_EXCEPTION(exceptions::codec_error, "signed envelope missing required fields");
   }
   return out;
}

signed_envelope signed_envelope::seal(const public_key& key, const fcl::crypto::asymmetric::private_key& private_key,
                                      std::string_view domain,
                                      std::span<const std::uint8_t> payload_type,
                                      std::span<const std::uint8_t> payload) {
   if (payload_type.empty() || payload.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "signed envelope payload and type must not be empty");
   }
   auto out = signed_envelope{
       .key = key,
       .payload_type = std::vector<std::uint8_t>{payload_type.begin(), payload_type.end()},
       .payload = std::vector<std::uint8_t>{payload.begin(), payload.end()},
   };
   const auto message = out.signing_payload(domain);
   out.signature = sign_identity(private_key, message);
   return out;
}

} // namespace fcl::p2p
