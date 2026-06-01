module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module fcl.p2p.identity;

import fcl.crypto.base58;
import fcl.crypto.ed25519;
import fcl.crypto.x509;
import fcl.multiformats;

namespace fcl::p2p {

namespace {

void append_varint(fcl::multiformats::bytes& out, std::uint64_t value) {
   auto encoded = fcl::multiformats::varint_encode(value);
   out.insert(out.end(), encoded.begin(), encoded.end());
}

[[nodiscard]] peer_id peer_id_from_multihash(const fcl::multiformats::multihash& hash) {
   return peer_id::from_bytes(hash.encode());
}

[[nodiscard]] bool has_cid_v1_prefix(std::string_view value) noexcept {
   return !value.empty() && (value.front() == 'b' || value.front() == 'B');
}

[[nodiscard]] std::size_t read_der_length(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   if (offset >= bytes.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension is truncated");
   }
   const auto first = bytes[offset++];
   if ((first & 0x80U) == 0) {
      return first;
   }
   const auto count = static_cast<std::size_t>(first & 0x7fU);
   if (count == 0 || count > sizeof(std::size_t) || offset + count > bytes.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "invalid libp2p certificate extension length");
   }
   auto out = std::size_t{};
   for (auto i = std::size_t{}; i < count; ++i) {
      out = (out << 8U) | bytes[offset++];
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> read_der_octet(std::span<const std::uint8_t> bytes, std::size_t& offset) {
   if (offset >= bytes.size() || bytes[offset++] != 0x04) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension expected octet string");
   }
   const auto length = read_der_length(bytes, offset);
   if (offset + length > bytes.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension octet string is truncated");
   }
   auto out = std::vector<std::uint8_t>{bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + length)};
   offset += length;
   return out;
}

struct signed_key_extension {
   std::vector<std::uint8_t> public_key;
   std::vector<std::uint8_t> signature;
};

[[nodiscard]] signed_key_extension decode_signed_key_extension(std::span<const std::uint8_t> bytes) {
   auto offset = std::size_t{};
   if (bytes.empty() || bytes[offset++] != 0x30) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension expected sequence");
   }
   const auto length = read_der_length(bytes, offset);
   if (offset + length != bytes.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension sequence length mismatch");
   }
   auto out = signed_key_extension{};
   out.public_key = read_der_octet(bytes, offset);
   out.signature = read_der_octet(bytes, offset);
   if (offset != bytes.size() || out.public_key.empty() || out.signature.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension is incomplete");
   }
   return out;
}

void verify_libp2p_certificate_extension(const fcl::crypto::x509::certificate& certificate,
                                         const signed_key_extension& extension) {
   const auto public_key = decode_public_key(extension.public_key);
   if (public_key.type != decltype(public_key.type)::ed25519) {
      return;
   }
   const auto spki = certificate.public_key_der();
   auto message = std::vector<std::uint8_t>{};
   constexpr auto prefix = std::string_view{"libp2p-tls-handshake:"};
   message.insert(message.end(), prefix.begin(), prefix.end());
   message.insert(message.end(), spki.begin(), spki.end());

   if (public_key.data.size() != fcl::crypto::ed25519::public_key_data{}.size() ||
       extension.signature.size() != fcl::crypto::ed25519::signature_data{}.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension has invalid Ed25519 sizes");
   }
   auto key_data = fcl::crypto::ed25519::public_key_data{};
   std::copy(public_key.data.begin(), public_key.data.end(), key_data.begin());
   auto signature_data = fcl::crypto::ed25519::signature_data{};
   std::copy(extension.signature.begin(), extension.signature.end(), signature_data.begin());
   if (!fcl::crypto::ed25519::public_key{key_data}.verify(message, signature_data)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p certificate extension signature is invalid");
   }
}

[[nodiscard]] std::optional<peer_id> peer_id_from_libp2p_certificate_extension(
    const fcl::crypto::x509::certificate& certificate) {
   const auto value = certificate.extension("1.3.6.1.4.1.53594.1.1");
   if (value.empty()) {
      return std::nullopt;
   }
   const auto extension = decode_signed_key_extension(value);
   verify_libp2p_certificate_extension(certificate, extension);
   return make_peer_id(decode_public_key(extension.public_key));
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
         FCL_THROW_EXCEPTION(exceptions::invalid_identity, "Peer ID CID uses an unsupported CID version");
      }
      std::size_t consumed = 0;
      const auto codec = fcl::multiformats::multicodec_decode(span.subspan(version.size), consumed);
      if (codec != fcl::multiformats::multicodec_code::libp2p_key) {
         FCL_THROW_EXCEPTION(exceptions::invalid_identity, "Peer ID CID is not a libp2p-key CID");
      }
      auto multihash = fcl::multiformats::multihash::decode(span.subspan(version.size + consumed));
      return from_bytes(multihash.encode());
   }

   auto id = peer_id{.value = std::string{value}};
   if (!valid_peer_id(id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "Peer ID string is not a valid libp2p multihash");
   }
   return id;
}

peer_id peer_id::from_bytes(std::span<const std::uint8_t> value) {
   (void)fcl::multiformats::multihash::decode(value);
   return peer_id{.value = fcl::crypto::base58_encode(value)};
}

fcl::multiformats::bytes encode_public_key(const public_key& key) {
   if (key.data.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "libp2p public key data is empty");
   }

   auto out = fcl::multiformats::bytes{};
   append_varint(out, 0x08);
   append_varint(out, static_cast<std::uint8_t>(key.type));
   append_varint(out, 0x12);
   append_varint(out, key.data.size());
   out.insert(out.end(), key.data.begin(), key.data.end());
   return out;
}

public_key decode_public_key(std::span<const std::uint8_t> bytes) {
   auto out = public_key{};
   auto offset = std::size_t{};
   auto saw_type = false;
   auto saw_data = false;
   while (offset < bytes.size()) {
      const auto key = fcl::multiformats::varint_decode(bytes.subspan(offset));
      offset += key.size;
      const auto field = static_cast<std::uint32_t>(key.value >> 3U);
      const auto wire = static_cast<std::uint8_t>(key.value & 0x07U);
      switch (field) {
      case 1: {
         if (wire != 0) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p public key type must be varint");
         }
         const auto value = fcl::multiformats::varint_decode(bytes.subspan(offset));
         offset += value.size;
         out.type = static_cast<decltype(out.type)>(value.value);
         saw_type = true;
         break;
      }
      case 2: {
         if (wire != 2) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p public key data must be bytes");
         }
         const auto size = fcl::multiformats::varint_decode(bytes.subspan(offset));
         offset += size.size;
         if (size.value > bytes.size() - offset) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "truncated libp2p public key data");
         }
         out.data = fcl::multiformats::bytes{
             bytes.begin() + static_cast<std::ptrdiff_t>(offset),
             bytes.begin() + static_cast<std::ptrdiff_t>(offset + static_cast<std::size_t>(size.value))};
         offset += static_cast<std::size_t>(size.value);
         saw_data = true;
         break;
      }
      default:
         FCL_THROW_EXCEPTION(exceptions::codec_error, "unsupported libp2p public key field");
      }
   }
   if (!saw_type || !saw_data || out.data.empty()) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "libp2p public key is incomplete");
   }
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
   auto certificate = fcl::crypto::x509::certificate::from_pem(certificate_pem);
   if (auto id = peer_id_from_libp2p_certificate_extension(certificate)) {
      return *id;
   }
   auto id = peer_id_from_multihash(fcl::multiformats::multihash::sha2_256(certificate.der()));
   if (!valid_peer_id(id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "certificate did not produce a valid P2P peer id");
   }
   return id;
}

peer_id make_peer_id_from_certificate_der(std::span<const std::uint8_t> certificate_der) {
   auto parsed = fcl::crypto::x509::certificate::from_der(certificate_der);
   if (auto id = peer_id_from_libp2p_certificate_extension(parsed)) {
      return *id;
   }
   auto id = peer_id_from_multihash(fcl::multiformats::multihash::sha2_256(certificate_der));
   if (!valid_peer_id(id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, "peer certificate did not produce a valid P2P peer id");
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
