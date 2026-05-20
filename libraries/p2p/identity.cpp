module;

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module fcl.p2p.identity;

import fcl.crypto.base58;
import fcl.multiformats;

namespace fcl::p2p {

namespace {

void append_varint(fcl::multiformats::bytes& out, std::uint64_t value) {
   auto encoded = fcl::multiformats::varint_encode(value);
   out.insert(out.end(), encoded.begin(), encoded.end());
}

[[nodiscard]] fcl::multiformats::bytes bytes_from_text(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] peer_id peer_id_from_multihash(const fcl::multiformats::multihash& hash) {
   return peer_id::from_bytes(hash.encode());
}

[[nodiscard]] bool has_cid_v1_prefix(std::string_view value) noexcept {
   return !value.empty() && (value.front() == 'b' || value.front() == 'B');
}

} // namespace

std::string peer_id::to_string() const {
   return value;
}

std::string peer_id::to_cid_string() const {
   auto cid = fcl::multiformats::varint_encode(1);
   append_varint(cid, fcl::multiformats::code_value(fcl::multiformats::multicodec_code::libp2p_key));
   auto multihash = to_bytes();
   cid.insert(cid.end(), multihash.begin(), multihash.end());
   return fcl::multiformats::multibase_encode(fcl::multiformats::multibase_code::base32, cid);
}

fcl::multiformats::bytes peer_id::to_bytes() const {
   return fcl::crypto::base58_decode(value);
}

peer_id peer_id::from_string(std::string_view value) {
   if (has_cid_v1_prefix(value)) {
      auto decoded = fcl::multiformats::multibase_decode(value);
      auto span = std::span<const std::uint8_t>{decoded.bytes};
      auto version = fcl::multiformats::varint_decode(span);
      if (version.value != 1) {
         throw_p2p_error(error_kind::invalid_identity, "Peer ID CID uses an unsupported CID version");
      }
      std::size_t consumed = 0;
      const auto codec = fcl::multiformats::multicodec_decode(span.subspan(version.size), consumed);
      if (codec != fcl::multiformats::multicodec_code::libp2p_key) {
         throw_p2p_error(error_kind::invalid_identity, "Peer ID CID is not a libp2p-key CID");
      }
      auto multihash = fcl::multiformats::multihash::decode(span.subspan(version.size + consumed));
      return from_bytes(multihash.encode());
   }

   auto id = peer_id{.value = std::string{value}};
   if (!valid_peer_id(id)) {
      throw_p2p_error(error_kind::invalid_identity, "Peer ID string is not a valid libp2p multihash");
   }
   return id;
}

peer_id peer_id::from_bytes(std::span<const std::uint8_t> value) {
   (void)fcl::multiformats::multihash::decode(value);
   return peer_id{.value = fcl::crypto::base58_encode(value)};
}

fcl::multiformats::bytes encode_public_key(const public_key& key) {
   if (key.data.empty()) {
      throw_p2p_error(error_kind::invalid_identity, "libp2p public key data is empty");
   }

   auto out = fcl::multiformats::bytes{};
   append_varint(out, 0x08);
   append_varint(out, static_cast<std::uint8_t>(key.type));
   append_varint(out, 0x12);
   append_varint(out, key.data.size());
   out.insert(out.end(), key.data.begin(), key.data.end());
   return out;
}

peer_id make_peer_id(const public_key& key) {
   auto encoded = encode_public_key(key);
   if (encoded.size() <= 42) {
      return peer_id_from_multihash(fcl::multiformats::multihash::identity(encoded));
   }
   return peer_id_from_multihash(fcl::multiformats::multihash::sha2_256(encoded));
}

peer_id make_peer_id_from_certificate_pem(std::string_view certificate_pem) {
   auto fingerprint = fcl::quic::certificate_sha256_fingerprint_from_pem(certificate_pem);
   auto id = peer_id_from_multihash(fcl::multiformats::multihash::sha2_256(bytes_from_text(fingerprint)));
   if (!valid_peer_id(id)) {
      throw_p2p_error(error_kind::invalid_identity, "certificate did not produce a valid P2P peer id");
   }
   return id;
}

peer_id make_peer_id_from_certificate(const fcl::quic::peer_certificate& certificate) {
   auto id = peer_id_from_multihash(fcl::multiformats::multihash::sha2_256(certificate.der));
   if (!valid_peer_id(id)) {
      throw_p2p_error(error_kind::invalid_identity, "peer certificate did not produce a valid P2P peer id");
   }
   return id;
}

bool valid_peer_id(const peer_id& id) noexcept {
   try {
      (void)fcl::multiformats::multihash::decode(id.to_bytes());
      return true;
   } catch (...) {
      return false;
   }
}

} // namespace fcl::p2p
