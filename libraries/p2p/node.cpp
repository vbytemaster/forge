module;

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

module fcl.p2p.node;

import fcl.crypto.chacha20_poly1305;
import fcl.crypto.der;
import fcl.crypto.ed25519;
import fcl.crypto.hmac;
import fcl.crypto.pem;
import fcl.crypto.public_key;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.hole_punch;
import fcl.p2p.identify;
import fcl.p2p.exceptions;
import fcl.p2p.message;
import fcl.p2p.negotiation;
import fcl.p2p.reachability;
import fcl.p2p.resource_manager;
import fcl.p2p.scoring;
import fcl.p2p.stream;
import fcl.crypto.private_key;
import fcl.crypto.random;
import fcl.crypto.rsa;
import fcl.crypto.sha256;
import fcl.crypto.x25519;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.exceptions;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.exceptions;
import fcl.quic.framed_stream;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.security;

#include "protobuf.hpp"

namespace fcl::p2p {
namespace asio = boost::asio;

namespace {

inline constexpr std::uint16_t peer_exchange_wire_version_v1 = 1;
inline constexpr std::uint32_t mandatory_flag_mask = 0x8000'0000U;

void trace_relay(std::string_view message) {
   (void)message;
}

struct peer_exchange_message {
   enum class type : std::uint16_t {
      hello = 1,
      peer_exchange_request = 6,
      peer_exchange_response = 7,
      ping = 12,
      pong = 13,
      goaway = 14
   };

   struct endpoint_record {
      peer_id peer;
      fcl::quic::endpoint endpoint;
      capability_set capabilities{};
   };

   type kind = type::ping;
   std::uint64_t request_id = 0;
   std::uint32_t flags = 0;
   peer_id peer;
   protocol_id protocol;
   capability_set capabilities{};
   std::uint64_t max_frame_size = 16 * 1024 * 1024;
   std::string reason;
   std::vector<endpoint_record> endpoints;
   std::vector<std::uint8_t> payload;
};

namespace peer_exchange_codec {

inline constexpr std::uint8_t magic[] = {'S', 'L', 'P', '2'};

struct options {
   std::uint32_t max_message_size = 4 * 1024 * 1024;
   std::uint32_t max_endpoint_records = 1024;
};

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
   out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
   out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
   for (auto shift = 24; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
   }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
   for (auto shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
   }
}

void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
   if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      exceptions::raise(exceptions::code::codec_error, "P2P string field is too large");
   }
   append_u32(out, static_cast<std::uint32_t>(value.size()));
   out.insert(out.end(), value.begin(), value.end());
}

void append_bytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& value) {
   if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      exceptions::raise(exceptions::code::codec_error, "P2P bytes field is too large");
   }
   append_u32(out, static_cast<std::uint32_t>(value.size()));
   out.insert(out.end(), value.begin(), value.end());
}

class reader {
 public:
   explicit reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

   [[nodiscard]] std::uint16_t u16() {
      require(2);
      const auto out = static_cast<std::uint16_t>((bytes_[offset_] << 8) | bytes_[offset_ + 1]);
      offset_ += 2;
      return out;
   }

   [[nodiscard]] std::uint32_t u32() {
      require(4);
      auto out = std::uint32_t{0};
      for (auto i = 0; i != 4; ++i) {
         out = (out << 8) | bytes_[offset_ + i];
      }
      offset_ += 4;
      return out;
   }

   [[nodiscard]] std::uint64_t u64() {
      require(8);
      auto out = std::uint64_t{0};
      for (auto i = 0; i != 8; ++i) {
         out = (out << 8) | bytes_[offset_ + i];
      }
      offset_ += 8;
      return out;
   }

   [[nodiscard]] std::string string() {
      const auto size = u32();
      require(size);
      auto out = std::string{};
      out.reserve(size);
      for (std::size_t i = 0; i < size; ++i) {
         out.push_back(static_cast<char>(bytes_[offset_ + i]));
      }
      offset_ += size;
      return out;
   }

   [[nodiscard]] std::vector<std::uint8_t> bytes() {
      const auto size = u32();
      require(size);
      auto out = std::vector<std::uint8_t>{bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                           bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size)};
      offset_ += size;
      return out;
   }

   void require_magic() {
      require(sizeof(magic));
      if (std::memcmp(bytes_.data() + offset_, magic, sizeof(magic)) != 0) {
         exceptions::raise(exceptions::code::codec_error, "invalid peer exchange message magic");
      }
      offset_ += sizeof(magic);
   }

   void expect_end() const {
      if (offset_ != bytes_.size()) {
         exceptions::raise(exceptions::code::codec_error, "trailing bytes in peer exchange message");
      }
   }

 private:
   void require(std::size_t size) const {
      if (size > bytes_.size() - offset_) {
         exceptions::raise(exceptions::code::codec_error, "truncated peer exchange message");
      }
   }

   std::span<const std::uint8_t> bytes_;
   std::size_t offset_ = 0;
};

[[nodiscard]] peer_exchange_message::type checked_kind(std::uint16_t value) {
   switch (static_cast<peer_exchange_message::type>(value)) {
   case peer_exchange_message::type::hello:
   case peer_exchange_message::type::peer_exchange_request:
   case peer_exchange_message::type::peer_exchange_response:
   case peer_exchange_message::type::ping:
   case peer_exchange_message::type::pong:
   case peer_exchange_message::type::goaway:
      return static_cast<peer_exchange_message::type>(value);
   }
   exceptions::raise(exceptions::code::codec_error, "unknown peer exchange message type");
}

[[nodiscard]] std::vector<std::uint8_t> encode(const peer_exchange_message& message, options opts = {}) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(128 + message.payload.size());
   out.insert(out.end(), std::begin(magic), std::end(magic));
   append_u16(out, peer_exchange_wire_version_v1);
   append_u16(out, static_cast<std::uint16_t>(message.kind));
   append_u32(out, message.flags);
   append_u64(out, message.request_id);
   append_string(out, message.peer.value);
   append_string(out, message.protocol.value);
   append_u64(out, message.capabilities.bits);
   append_u64(out, message.max_frame_size);
   append_string(out, message.reason);
   if (message.endpoints.size() > opts.max_endpoint_records) {
      exceptions::raise(exceptions::code::codec_error, "too many peer exchange endpoint records");
   }
   append_u32(out, static_cast<std::uint32_t>(message.endpoints.size()));
   for (const auto& endpoint : message.endpoints) {
      append_string(out, endpoint.peer.value);
      append_string(out, endpoint.endpoint.host);
      append_u16(out, endpoint.endpoint.port);
      append_u64(out, endpoint.capabilities.bits);
   }
   append_bytes(out, message.payload);
   if (out.size() > opts.max_message_size) {
      exceptions::raise(exceptions::code::codec_error, "peer exchange message exceeds max size");
   }
   return out;
}

[[nodiscard]] peer_exchange_message decode(std::span<const std::uint8_t> bytes, options opts = {}) {
   if (bytes.size() > opts.max_message_size) {
      exceptions::raise(exceptions::code::codec_error, "peer exchange message exceeds max size");
   }
   auto in = reader{bytes};
   in.require_magic();
   if (in.u16() != peer_exchange_wire_version_v1) {
      exceptions::raise(exceptions::code::codec_error, "unsupported peer exchange wire version");
   }
   auto out = peer_exchange_message{};
   out.kind = checked_kind(in.u16());
   out.flags = in.u32();
   if ((out.flags & mandatory_flag_mask) != 0) {
      exceptions::raise(exceptions::code::codec_error, "unknown mandatory peer exchange flags");
   }
   out.request_id = in.u64();
   out.peer = peer_id{.value = in.string()};
   out.protocol = protocol_id{.value = in.string()};
   out.capabilities = capability_set{.bits = in.u64()};
   out.max_frame_size = in.u64();
   out.reason = in.string();
   const auto endpoint_count = in.u32();
   if (endpoint_count > opts.max_endpoint_records) {
      exceptions::raise(exceptions::code::codec_error, "too many peer exchange endpoint records");
   }
   out.endpoints.reserve(endpoint_count);
   for (auto i = std::uint32_t{0}; i != endpoint_count; ++i) {
      out.endpoints.push_back(peer_exchange_message::endpoint_record{
          .peer = peer_id{.value = in.string()},
          .endpoint = fcl::quic::endpoint{.host = in.string(), .port = in.u16()},
          .capabilities = capability_set{.bits = in.u64()},
      });
   }
   out.payload = in.bytes();
   in.expect_end();
   return out;
}

boost::asio::awaitable<void> async_write(fcl::quic::framed_stream& stream, const peer_exchange_message& message,
                                         options opts = {}) {
   auto encoded = encode(message, opts);
   co_await stream.async_write_frame(encoded);
}

boost::asio::awaitable<peer_exchange_message> async_read(fcl::quic::framed_stream& stream, options opts = {}) {
   auto encoded = co_await stream.async_read_frame();
   co_return decode(encoded, opts);
}

} // namespace peer_exchange_codec

[[nodiscard]] exceptions::code map_quic_error(fcl::quic::exceptions::code kind) noexcept {
   using quic_kind = fcl::quic::exceptions::code;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return exceptions::code::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return exceptions::code::timeout;
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return exceptions::code::peer_verification_failed;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return exceptions::code::codec_error;
   case quic_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return exceptions::code::closed;
   case quic_kind::canceled:
      return exceptions::code::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal:
   case quic_kind::unsupported:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[nodiscard]] exceptions::code p2p_code(const fcl::exception::base& error) {
   const auto code = exceptions::code_of(error);
   if (!code) {
      throw;
   }
   return *code;
}

[[nodiscard]] fcl::quic::exceptions::code quic_code(const fcl::exception::base& error) {
   const auto code = fcl::quic::exceptions::code_of(error);
   if (!code) {
      throw;
   }
   return *code;
}

[[noreturn]] void rethrow_quic_as_p2p(const fcl::exception::base& error) {
   exceptions::raise(map_quic_error(quic_code(error)), error.what());
}

[[nodiscard]] bool is_orderly_stream_close(const fcl::exception::base& error) noexcept {
   return fcl::quic::exceptions::is(error, fcl::quic::exceptions::code::stream_closed);
}

[[nodiscard]] std::uint64_t random_nonce() {
   const auto bytes = fcl::crypto::random_bytes(8);
   auto out = std::uint64_t{};
   for (auto byte : bytes) {
      out = (out << 8U) | byte;
   }
   return out == 0 ? 1 : out;
}

[[noreturn]] void throw_crypto_failure(std::string message) {
   exceptions::raise(exceptions::code::invalid_identity, std::move(message));
}

[[nodiscard]] std::vector<std::uint8_t> sha256(std::span<const std::uint8_t> value) {
   const auto digest = fcl::crypto::sha256::hash(value).to_uint8_span();
   return {digest.begin(), digest.end()};
}

[[nodiscard]] std::vector<std::uint8_t> hmac_sha256(std::span<const std::uint8_t> key,
                                                    std::span<const std::uint8_t> value) {
   const auto digest = fcl::crypto::hmac_sha256{}.digest(key, value).to_uint8_span();
   return {digest.begin(), digest.end()};
}

[[nodiscard]] std::vector<std::uint8_t> concat(std::span<const std::uint8_t> left,
                                               std::span<const std::uint8_t> right) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(left.size() + right.size());
   out.insert(out.end(), left.begin(), left.end());
   out.insert(out.end(), right.begin(), right.end());
   return out;
}

[[nodiscard]] std::array<std::vector<std::uint8_t>, 2> noise_hkdf2(std::span<const std::uint8_t> chaining_key,
                                                                   std::span<const std::uint8_t> input) {
   const auto temp_key = hmac_sha256(chaining_key, input);
   const auto first_input = std::array<std::uint8_t, 1>{1};
   const auto out1 = hmac_sha256(temp_key, first_input);
   auto out2_input = out1;
   out2_input.push_back(2);
   return {out1, hmac_sha256(temp_key, out2_input)};
}

template <typename Range> [[nodiscard]] std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

[[nodiscard]] fcl::crypto::private_key private_key_from_pem(std::string_view pem) {
   try {
      return fcl::crypto::pem::read_private_key(pem);
   } catch (const fcl::exception::base& error) {
      throw_crypto_failure(error.what());
   }
}

[[nodiscard]] public_key public_key_from_crypto(const fcl::crypto::public_key& key) {
   return key.visit([](const auto& value) -> public_key {
      using value_type = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<value_type, fcl::crypto::ed25519::public_key_shim>) {
         return public_key{.type = public_key::type::ed25519, .data = bytes_from_range(value.serialize())};
      } else if constexpr (std::is_same_v<value_type, fcl::crypto::rsa::public_key_shim>) {
         return public_key{.type = public_key::type::rsa, .data = value.serialize()};
      } else {
         const auto spki = fcl::crypto::der::write_public_key(fcl::crypto::public_key{
             fcl::crypto::public_key::storage_type{value}});
         return public_key{.type = public_key::type::ecdsa, .data = spki};
      }
   });
}

[[nodiscard]] fcl::crypto::public_key crypto_public_key(const public_key& key) {
   if (key.type == public_key::type::ed25519) {
      if (key.data.size() != fcl::crypto::ed25519::public_key_data{}.size()) {
         exceptions::raise(exceptions::code::invalid_identity, "invalid Ed25519 public key size");
      }
      auto data = fcl::crypto::ed25519::public_key_data{};
      std::copy(key.data.begin(), key.data.end(), data.begin());
      return fcl::crypto::public_key{
          fcl::crypto::public_key::storage_type{fcl::crypto::ed25519::public_key_shim{data}}};
   }
   if (key.type == public_key::type::rsa) {
      return fcl::crypto::public_key{
          fcl::crypto::public_key::storage_type{fcl::crypto::rsa::public_key_shim{key.data}}};
   }
   try {
      return fcl::crypto::der::read_public_key(key.data);
   } catch (const fcl::exception::base& error) {
      throw_crypto_failure(error.what());
   }
}

[[nodiscard]] public_key public_key_from_private(const fcl::crypto::private_key& key) {
   return public_key_from_crypto(key.get_public_key());
}

[[nodiscard]] std::vector<std::uint8_t> sign_identity(const fcl::crypto::private_key& key,
                                                      std::span<const std::uint8_t> message) {
   try {
      const auto signature = key.sign(message);
      return signature.visit([](const auto& value) { return bytes_from_range(value.serialize()); });
   } catch (const fcl::exception::base& error) {
      throw_crypto_failure(error.what());
   }
}

[[nodiscard]] bool verify_identity_signature(const public_key& key, std::span<const std::uint8_t> message,
                                             std::span<const std::uint8_t> signature) {
   if (key.type == public_key::type::ed25519) {
      if (signature.size() != fcl::crypto::ed25519::signature_data{}.size()) {
         return false;
      }
      auto value = fcl::crypto::ed25519::signature_data{};
      std::copy(signature.begin(), signature.end(), value.begin());
      return crypto_public_key(key).as<fcl::crypto::ed25519::public_key_shim>().verify(message, value);
   }
   if (key.type == public_key::type::rsa) {
      return fcl::crypto::rsa::public_key{key.data}.verify(message, {signature.begin(), signature.end()});
   }
   exceptions::raise(exceptions::code::invalid_identity, "ECDSA Noise identity verification requires DER signature support");
}

struct x25519_key {
   fcl::crypto::x25519::private_key key;
   std::array<std::uint8_t, 32> public_key{};
};

[[nodiscard]] x25519_key make_x25519_key() {
   auto key = fcl::crypto::x25519::private_key::generate();
   return x25519_key{.key = key, .public_key = key.get_public_key().serialize()};
}

[[nodiscard]] std::vector<std::uint8_t> x25519_dh(const fcl::crypto::x25519::private_key& private_key,
                                                  std::span<const std::uint8_t, 32> remote_public) {
   auto public_key = fcl::crypto::x25519::public_key_data{};
   std::copy(remote_public.begin(), remote_public.end(), public_key.begin());
   const auto secret = private_key.get_shared_secret(fcl::crypto::x25519::public_key{public_key});
   return {secret.begin(), secret.end()};
}

[[nodiscard]] std::array<std::uint8_t, 32> checked_x25519_public(std::span<const std::uint8_t> bytes) {
   if (bytes.size() != 32) {
      exceptions::raise(exceptions::code::protocol_error, "Noise X25519 public key must be 32 bytes");
   }
   auto out = std::array<std::uint8_t, 32>{};
   std::copy(bytes.begin(), bytes.end(), out.begin());
   return out;
}

[[nodiscard]] std::array<std::uint8_t, 12> noise_nonce(std::uint64_t value) {
   auto out = std::array<std::uint8_t, 12>{};
   for (auto index = 0; index != 8; ++index) {
      out[4 + index] = static_cast<std::uint8_t>((value >> (8U * index)) & 0xffU);
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> chacha20_poly1305_encrypt(std::span<const std::uint8_t> key,
                                                                  std::uint64_t nonce_value,
                                                                  std::span<const std::uint8_t> ad,
                                                                  std::span<const std::uint8_t> plaintext) {
   if (key.size() != fcl::crypto::chacha20_poly1305::key{}.size()) {
      exceptions::raise(exceptions::code::protocol_error, "Noise cipher key must be 32 bytes");
   }
   auto cipher_key = fcl::crypto::chacha20_poly1305::key{};
   std::copy(key.begin(), key.end(), cipher_key.begin());
   const auto nonce = noise_nonce(nonce_value);
   return fcl::crypto::chacha20_poly1305::encrypt(cipher_key, nonce, ad, plaintext);
}

[[nodiscard]] std::vector<std::uint8_t> chacha20_poly1305_decrypt(std::span<const std::uint8_t> key,
                                                                  std::uint64_t nonce_value,
                                                                  std::span<const std::uint8_t> ad,
                                                                  std::span<const std::uint8_t> ciphertext) {
   if (ciphertext.size() < 16) {
      exceptions::raise(exceptions::code::protocol_error, "Noise ciphertext is missing authentication tag");
   }
   if (key.size() != fcl::crypto::chacha20_poly1305::key{}.size()) {
      exceptions::raise(exceptions::code::protocol_error, "Noise cipher key must be 32 bytes");
   }
   auto cipher_key = fcl::crypto::chacha20_poly1305::key{};
   std::copy(key.begin(), key.end(), cipher_key.begin());
   const auto nonce = noise_nonce(nonce_value);
   try {
      return fcl::crypto::chacha20_poly1305::decrypt(cipher_key, nonce, ad, ciphertext);
   } catch (const fcl::exception::base&) {
      exceptions::raise(exceptions::code::peer_verification_failed, "Noise authentication failed");
   }
}

struct noise_cipher_state {
   std::vector<std::uint8_t> key;
   std::uint64_t nonce = 0;

   [[nodiscard]] bool has_key() const noexcept {
      return !key.empty();
   }

   [[nodiscard]] std::vector<std::uint8_t> encrypt(std::span<const std::uint8_t> ad,
                                                   std::span<const std::uint8_t> plaintext) {
      if (!has_key()) {
         return {plaintext.begin(), plaintext.end()};
      }
      auto out = chacha20_poly1305_encrypt(key, nonce, ad, plaintext);
      ++nonce;
      return out;
   }

   [[nodiscard]] std::vector<std::uint8_t> decrypt(std::span<const std::uint8_t> ad,
                                                   std::span<const std::uint8_t> ciphertext) {
      if (!has_key()) {
         return {ciphertext.begin(), ciphertext.end()};
      }
      auto out = chacha20_poly1305_decrypt(key, nonce, ad, ciphertext);
      ++nonce;
      return out;
   }
};

struct noise_symmetric_state {
   std::vector<std::uint8_t> chaining_key;
   std::vector<std::uint8_t> hash;
   noise_cipher_state cipher;

   noise_symmetric_state() {
      constexpr auto protocol = std::string_view{"Noise_XX_25519_ChaChaPoly_SHA256"};
      hash.assign(32, 0);
      std::copy(protocol.begin(), protocol.end(), hash.begin());
      chaining_key = hash;
      mix_hash(std::span<const std::uint8_t>{});
   }

   void mix_hash(std::span<const std::uint8_t> value) {
      hash = sha256(concat(hash, value));
   }

   void mix_key(std::span<const std::uint8_t> input) {
      auto keys = noise_hkdf2(chaining_key, input);
      chaining_key = std::move(keys[0]);
      cipher.key = std::move(keys[1]);
      cipher.nonce = 0;
   }

   [[nodiscard]] std::vector<std::uint8_t> encrypt_and_hash(std::span<const std::uint8_t> plaintext) {
      auto ciphertext = cipher.encrypt(hash, plaintext);
      mix_hash(ciphertext);
      return ciphertext;
   }

   [[nodiscard]] std::vector<std::uint8_t> decrypt_and_hash(std::span<const std::uint8_t> ciphertext) {
      auto plaintext = cipher.decrypt(hash, ciphertext);
      mix_hash(ciphertext);
      return plaintext;
   }

   [[nodiscard]] std::array<noise_cipher_state, 2> split() const {
      const auto empty = std::span<const std::uint8_t>{};
      const auto keys = noise_hkdf2(chaining_key, empty);
      return {noise_cipher_state{.key = keys[0]}, noise_cipher_state{.key = keys[1]}};
   }
};

[[nodiscard]] std::vector<std::uint8_t> noise_signature_payload(std::span<const std::uint8_t> static_key) {
   auto out = std::vector<std::uint8_t>{};
   constexpr auto prefix = std::string_view{"noise-libp2p-static-key:"};
   out.insert(out.end(), prefix.begin(), prefix.end());
   out.insert(out.end(), static_key.begin(), static_key.end());
   return out;
}

struct noise_handshake_payload {
   std::vector<std::uint8_t> identity_key;
   std::vector<std::uint8_t> identity_signature;
   std::vector<std::string> stream_muxers;
};

[[nodiscard]] std::vector<std::uint8_t> encode_noise_payload(const noise_handshake_payload& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.identity_key);
   detail::append_bytes(out, 2, value.identity_signature);
   auto extensions = std::vector<std::uint8_t>{};
   for (const auto& muxer : value.stream_muxers) {
      detail::append_string(extensions, 2, muxer);
   }
   if (!extensions.empty()) {
      detail::append_bytes(out, 4, extensions);
   }
   return out;
}

[[nodiscard]] noise_handshake_payload decode_noise_payload(std::span<const std::uint8_t> bytes) {
   auto out = noise_handshake_payload{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (type != detail::wire_type::length_delimited) {
         in.skip(type);
         continue;
      }
      switch (field) {
      case 1:
         out.identity_key = in.bytes();
         break;
      case 2:
         out.identity_signature = in.bytes();
         break;
      case 4: {
         auto ext_bytes = in.bytes();
         auto ext = detail::reader{ext_bytes};
         while (!ext.done()) {
            const auto [ext_field, ext_type] = ext.key();
            if (ext_field == 2 && ext_type == detail::wire_type::length_delimited) {
               out.stream_muxers.push_back(ext.string());
            } else {
               ext.skip(ext_type);
            }
         }
         break;
      }
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

[[nodiscard]] noise_handshake_payload make_noise_payload(const node::options& options,
                                                         std::span<const std::uint8_t> static_key) {
   if (options.private_key_pem.empty()) {
      exceptions::raise(exceptions::code::invalid_identity, "Noise handshake requires libp2p identity key material");
   }
   auto private_key = private_key_from_pem(options.private_key_pem);
   auto identity_key = options.public_key;
   if (identity_key.empty()) {
      identity_key = encode_public_key(public_key_from_private(private_key));
   }
   return noise_handshake_payload{
       .identity_key = std::move(identity_key),
       .identity_signature = sign_identity(private_key, noise_signature_payload(static_key)),
       .stream_muxers = {"/yamux/1.0.0"},
   };
}

struct verified_noise_payload {
   peer_id peer;
   bool supports_yamux = false;
};

[[nodiscard]] verified_noise_payload verify_noise_payload(const noise_handshake_payload& payload,
                                                          std::span<const std::uint8_t> static_key,
                                                          const std::optional<peer_id>& expected_peer) {
   if (payload.identity_key.empty() || payload.identity_signature.empty()) {
      exceptions::raise(exceptions::code::peer_verification_failed, "Noise handshake payload is missing identity proof");
   }
   const auto key = decode_public_key(payload.identity_key);
   const auto peer = make_peer_id(key);
   if (expected_peer && peer != *expected_peer) {
      exceptions::raise(exceptions::code::peer_verification_failed, "Noise identity peer id mismatch");
   }
   if (!verify_identity_signature(key, noise_signature_payload(static_key), payload.identity_signature)) {
      exceptions::raise(exceptions::code::peer_verification_failed, "Noise identity signature is invalid");
   }
   return verified_noise_payload{
       .peer = peer,
       .supports_yamux = std::ranges::contains(payload.stream_muxers, std::string{"/yamux/1.0.0"}),
   };
}

class relay_secure_io : public std::enable_shared_from_this<relay_secure_io> {
 public:
   explicit relay_secure_io(fcl::p2p::stream stream) : stream_(std::move(stream)) {}

   [[nodiscard]] bool valid() const noexcept {
      return stream_.valid();
   }

   [[nodiscard]] std::int64_t id() const noexcept {
      return stream_.id();
   }

   boost::asio::awaitable<void> write_plain_frame(std::span<const std::uint8_t> bytes) {
      if (bytes.size() > std::numeric_limits<std::uint16_t>::max()) {
         exceptions::raise(exceptions::code::codec_error, "Noise frame is too large");
      }
      auto out = std::vector<std::uint8_t>{
          static_cast<std::uint8_t>((bytes.size() >> 8U) & 0xffU),
          static_cast<std::uint8_t>(bytes.size() & 0xffU),
      };
      out.insert(out.end(), bytes.begin(), bytes.end());
      co_await stream_.async_write(out);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> read_plain_frame() {
      const auto header = co_await read_exact(2);
      const auto size = (static_cast<std::uint16_t>(header[0]) << 8U) | header[1];
      co_return co_await read_exact(size);
   }

   void set_cipher_states(noise_cipher_state read_state, noise_cipher_state write_state) {
      read_state_ = std::move(read_state);
      write_state_ = std::move(write_state);
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) {
      auto encrypted = write_state_.encrypt({}, bytes);
      co_await write_plain_frame(encrypted);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() {
      auto encrypted = co_await read_plain_frame();
      auto plain = read_state_.decrypt({}, encrypted);
      co_return plain;
   }

   boost::asio::awaitable<void> async_close() {
      co_await stream_.async_close();
   }

 private:
   boost::asio::awaitable<std::vector<std::uint8_t>> read_exact(std::size_t size) {
      while (buffer_.size() < size) {
         auto chunk = co_await stream_.async_read();
         if (chunk.empty()) {
            exceptions::raise(exceptions::code::closed, "Noise stream closed");
         }
         buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
      }
      auto out = std::vector<std::uint8_t>{buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size)};
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size));
      co_return out;
   }

   fcl::p2p::stream stream_;
   std::vector<std::uint8_t> buffer_;
   noise_cipher_state read_state_;
   noise_cipher_state write_state_;
};

class relay_secure_stream_backend final : public detail::stream_backend {
 public:
   explicit relay_secure_stream_backend(std::shared_ptr<relay_secure_io> secure) : secure_(std::move(secure)) {}

   [[nodiscard]] bool valid() const noexcept override {
      return secure_ && secure_->valid();
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return secure_ ? secure_->id() : -1;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      co_await secure_->async_write(bytes);
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      co_return co_await secure_->async_read();
   }

   boost::asio::awaitable<void> async_close() override {
      co_await secure_->async_close();
   }

 private:
   std::shared_ptr<relay_secure_io> secure_;
};

[[nodiscard]] fcl::p2p::stream secure_stream(std::shared_ptr<relay_secure_io> secure) {
   return detail::stream_access::make(std::make_shared<relay_secure_stream_backend>(std::move(secure)));
}

struct noise_result {
   std::shared_ptr<relay_secure_io> secure;
   bool early_yamux = false;
};

boost::asio::awaitable<noise_result> noise_initiator(fcl::p2p::stream stream, const node::options& options,
                                                     std::optional<peer_id> expected_peer) {
   auto io = std::make_shared<relay_secure_io>(std::move(stream));
   auto symmetric = noise_symmetric_state{};
   auto ephemeral = make_x25519_key();
   auto local_static = make_x25519_key();

   symmetric.mix_hash(ephemeral.public_key);
   (void)symmetric.encrypt_and_hash(std::span<const std::uint8_t>{});
   co_await io->write_plain_frame(ephemeral.public_key);

   auto message2 = co_await io->read_plain_frame();
   if (message2.size() < 48) {
      exceptions::raise(exceptions::code::protocol_error, "Noise responder message is truncated");
   }
   const auto responder_ephemeral = checked_x25519_public(std::span<const std::uint8_t>{message2}.subspan(0, 32));
   symmetric.mix_hash(responder_ephemeral);
   symmetric.mix_key(x25519_dh(ephemeral.key, responder_ephemeral));
   const auto responder_static_cipher = std::span<const std::uint8_t>{message2}.subspan(32, 48);
   const auto responder_static_plain = symmetric.decrypt_and_hash(responder_static_cipher);
   const auto responder_static = checked_x25519_public(responder_static_plain);
   symmetric.mix_key(x25519_dh(ephemeral.key, responder_static));
   const auto responder_payload = symmetric.decrypt_and_hash(std::span<const std::uint8_t>{message2}.subspan(80));
   auto decoded_responder_payload = decode_noise_payload(responder_payload);
   const auto verified_responder = verify_noise_payload(decoded_responder_payload, responder_static, expected_peer);

   auto message3 = symmetric.encrypt_and_hash(local_static.public_key);
   symmetric.mix_key(x25519_dh(local_static.key, responder_ephemeral));
   const auto payload = encode_noise_payload(make_noise_payload(options, local_static.public_key));
   auto encrypted_payload = symmetric.encrypt_and_hash(payload);
   message3.insert(message3.end(), encrypted_payload.begin(), encrypted_payload.end());
   co_await io->write_plain_frame(message3);

   auto states = symmetric.split();
   io->set_cipher_states(std::move(states[1]), std::move(states[0]));
   co_return noise_result{.secure = std::move(io), .early_yamux = verified_responder.supports_yamux};
}

boost::asio::awaitable<noise_result> noise_responder(fcl::p2p::stream stream, const node::options& options,
                                                     std::optional<peer_id> expected_peer) {
   auto io = std::make_shared<relay_secure_io>(std::move(stream));
   auto symmetric = noise_symmetric_state{};
   auto initiator_ephemeral = checked_x25519_public(co_await io->read_plain_frame());
   symmetric.mix_hash(initiator_ephemeral);
   (void)symmetric.decrypt_and_hash(std::span<const std::uint8_t>{});

   auto ephemeral = make_x25519_key();
   auto local_static = make_x25519_key();
   auto message2 = std::vector<std::uint8_t>{ephemeral.public_key.begin(), ephemeral.public_key.end()};
   symmetric.mix_hash(ephemeral.public_key);
   symmetric.mix_key(x25519_dh(ephemeral.key, initiator_ephemeral));
   auto encrypted_static = symmetric.encrypt_and_hash(local_static.public_key);
   message2.insert(message2.end(), encrypted_static.begin(), encrypted_static.end());
   symmetric.mix_key(x25519_dh(local_static.key, initiator_ephemeral));
   const auto payload = encode_noise_payload(make_noise_payload(options, local_static.public_key));
   auto encrypted_payload = symmetric.encrypt_and_hash(payload);
   message2.insert(message2.end(), encrypted_payload.begin(), encrypted_payload.end());
   co_await io->write_plain_frame(message2);

   const auto message3 = co_await io->read_plain_frame();
   if (message3.size() < 48) {
      exceptions::raise(exceptions::code::protocol_error, "Noise initiator message is truncated");
   }
   const auto initiator_static_plain =
       symmetric.decrypt_and_hash(std::span<const std::uint8_t>{message3}.subspan(0, 48));
   const auto initiator_static = checked_x25519_public(initiator_static_plain);
   symmetric.mix_key(x25519_dh(ephemeral.key, initiator_static));
   const auto initiator_payload = symmetric.decrypt_and_hash(std::span<const std::uint8_t>{message3}.subspan(48));
   auto decoded_initiator_payload = decode_noise_payload(initiator_payload);
   const auto verified_initiator = verify_noise_payload(decoded_initiator_payload, initiator_static, expected_peer);

   auto states = symmetric.split();
   io->set_cipher_states(std::move(states[0]), std::move(states[1]));
   co_return noise_result{.secure = std::move(io), .early_yamux = verified_initiator.supports_yamux};
}

struct yamux_frame {
   enum class type : std::uint8_t {
      data = 0,
      window_update = 1,
      ping = 2,
      go_away = 3,
   };

   type kind = type::data;
   std::uint16_t flags = 0;
   std::uint32_t stream_id = 0;
   std::vector<std::uint8_t> payload;
   std::uint32_t length_value = 0;
};

inline constexpr std::uint16_t yamux_syn = 0x01;
inline constexpr std::uint16_t yamux_ack = 0x02;
inline constexpr std::uint16_t yamux_fin = 0x04;
inline constexpr std::uint16_t yamux_rst = 0x08;
inline constexpr std::uint32_t yamux_initial_window = 256 * 1024;

void trace_yamux(std::string_view direction, const yamux_frame& frame) {
   (void)direction;
   (void)frame;
}

class yamux_session : public std::enable_shared_from_this<yamux_session> {
 public:
   yamux_session(std::shared_ptr<relay_secure_io> secure, bool initiator)
       : secure_(std::move(secure)), next_stream_id_(initiator ? 1U : 2U) {}

   boost::asio::awaitable<fcl::p2p::stream> async_open_stream() {
      const auto id = next_stream_id_;
      next_stream_id_ += 2;
      co_await write_frame(yamux_frame{
          .kind = yamux_frame::type::window_update,
          .flags = yamux_syn,
          .stream_id = id,
          .length_value = yamux_initial_window,
      });
      co_return detail::stream_access::make(std::make_shared<yamux_stream_backend>(shared_from_this(), id));
   }

   boost::asio::awaitable<fcl::p2p::stream> async_accept_stream() {
      if (!pending_streams_.empty()) {
         const auto id = pending_streams_.front();
         pending_streams_.erase(pending_streams_.begin());
         co_return detail::stream_access::make(std::make_shared<yamux_stream_backend>(shared_from_this(), id));
      }
      while (true) {
         auto frame = co_await read_frame();
         if (frame.kind == yamux_frame::type::ping && (frame.flags & yamux_ack) == 0) {
            co_await write_frame(yamux_frame{
                .kind = yamux_frame::type::ping,
                .flags = yamux_ack,
                .stream_id = 0,
                .payload = frame.payload,
                .length_value = frame.length_value,
            });
            continue;
         }
         if (frame.kind == yamux_frame::type::go_away) {
            exceptions::raise(exceptions::code::closed, "Yamux session closed");
         }
         if (frame.kind != yamux_frame::type::data && frame.kind != yamux_frame::type::window_update) {
            continue;
         }
         if ((frame.flags & yamux_syn) == 0) {
            if (!frame.payload.empty()) {
               pending_[frame.stream_id].push_back(std::move(frame.payload));
            }
            continue;
         }
         co_await accept_remote_stream(frame.stream_id);
         if (!frame.payload.empty()) {
            pending_[frame.stream_id].push_back(std::move(frame.payload));
         }
         co_return detail::stream_access::make(
             std::make_shared<yamux_stream_backend>(shared_from_this(), frame.stream_id));
      }
   }

   [[nodiscard]] bool has_pending_stream() const noexcept {
      return !pending_streams_.empty();
   }

   boost::asio::awaitable<void> async_close() {
      co_await secure_->async_close();
   }

   boost::asio::awaitable<void> write_data(std::uint32_t stream_id, std::span<const std::uint8_t> bytes,
                                           std::uint16_t flags = 0) {
      co_await write_frame(yamux_frame{
          .kind = yamux_frame::type::data,
          .flags = flags,
          .stream_id = stream_id,
          .payload = std::vector<std::uint8_t>{bytes.begin(), bytes.end()},
      });
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> read_data(std::uint32_t stream_id) {
      if (auto it = pending_.find(stream_id); it != pending_.end() && !it->second.empty()) {
         auto out = std::move(it->second.front());
         it->second.erase(it->second.begin());
         co_return out;
      }
      while (true) {
         auto frame = co_await read_frame();
         if (frame.kind == yamux_frame::type::ping && (frame.flags & yamux_ack) == 0) {
            co_await write_frame(yamux_frame{
                .kind = yamux_frame::type::ping,
                .flags = yamux_ack,
                .stream_id = 0,
                .payload = frame.payload,
                .length_value = frame.length_value,
            });
            continue;
         }
         if (frame.stream_id != stream_id) {
            if ((frame.kind == yamux_frame::type::data || frame.kind == yamux_frame::type::window_update) &&
                (frame.flags & yamux_syn) != 0) {
               co_await accept_remote_stream(frame.stream_id);
               if (std::ranges::find(pending_streams_, frame.stream_id) == pending_streams_.end()) {
                  pending_streams_.push_back(frame.stream_id);
               }
            }
            if (!frame.payload.empty()) {
               pending_[frame.stream_id].push_back(std::move(frame.payload));
            }
            continue;
         }
         if ((frame.flags & yamux_rst) != 0) {
            exceptions::raise(exceptions::code::closed, "Yamux stream was reset");
         }
         if (!frame.payload.empty()) {
            co_return frame.payload;
         }
         if ((frame.flags & yamux_fin) != 0) {
            exceptions::raise(exceptions::code::closed, "Yamux stream closed");
         }
      }
   }

   boost::asio::awaitable<void> close_stream(std::uint32_t stream_id) {
      co_await write_frame(yamux_frame{
          .kind = yamux_frame::type::data,
          .flags = yamux_fin,
          .stream_id = stream_id,
      });
   }

 private:
   boost::asio::awaitable<void> accept_remote_stream(std::uint32_t stream_id) {
      co_await write_frame(yamux_frame{
          .kind = yamux_frame::type::window_update,
          .flags = yamux_ack,
          .stream_id = stream_id,
          .length_value = yamux_initial_window,
      });
   }

   class yamux_stream_backend final : public detail::stream_backend {
    public:
      yamux_stream_backend(std::shared_ptr<yamux_session> session, std::uint32_t stream_id)
          : session_(std::move(session)), stream_id_(stream_id) {}

      [[nodiscard]] bool valid() const noexcept override {
         return session_ != nullptr;
      }

      [[nodiscard]] std::int64_t id() const noexcept override {
         return static_cast<std::int64_t>(stream_id_);
      }

      boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
         co_await session_->write_data(stream_id_, bytes);
      }

      boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
         co_return co_await session_->read_data(stream_id_);
      }

      boost::asio::awaitable<void> async_close() override {
         co_await session_->close_stream(stream_id_);
      }

    private:
      std::shared_ptr<yamux_session> session_;
      std::uint32_t stream_id_ = 0;
   };

   boost::asio::awaitable<void> write_frame(const yamux_frame& frame) {
      trace_yamux("write", frame);
      auto out = std::vector<std::uint8_t>{};
      const auto length = frame.payload.empty() ? frame.length_value : static_cast<std::uint32_t>(frame.payload.size());
      out.reserve(12 + frame.payload.size());
      out.push_back(0);
      out.push_back(static_cast<std::uint8_t>(frame.kind));
      out.push_back(static_cast<std::uint8_t>((frame.flags >> 8U) & 0xffU));
      out.push_back(static_cast<std::uint8_t>(frame.flags & 0xffU));
      for (auto shift : {24, 16, 8, 0}) {
         out.push_back(static_cast<std::uint8_t>((frame.stream_id >> shift) & 0xffU));
      }
      for (auto shift : {24, 16, 8, 0}) {
         out.push_back(static_cast<std::uint8_t>((length >> shift) & 0xffU));
      }
      out.insert(out.end(), frame.payload.begin(), frame.payload.end());
      co_await secure_->async_write(out);
   }

   boost::asio::awaitable<yamux_frame> read_frame() {
      while (buffer_.size() < 12) {
         auto chunk = co_await secure_->async_read();
         buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
      }
      if (buffer_[0] != 0) {
         exceptions::raise(exceptions::code::protocol_error, "unsupported Yamux version");
      }
      auto frame = yamux_frame{
          .kind = static_cast<yamux_frame::type>(buffer_[1]),
          .flags = static_cast<std::uint16_t>((buffer_[2] << 8U) | buffer_[3]),
          .stream_id = (static_cast<std::uint32_t>(buffer_[4]) << 24U) |
                       (static_cast<std::uint32_t>(buffer_[5]) << 16U) |
                       (static_cast<std::uint32_t>(buffer_[6]) << 8U) | buffer_[7],
          .length_value = (static_cast<std::uint32_t>(buffer_[8]) << 24U) |
                          (static_cast<std::uint32_t>(buffer_[9]) << 16U) |
                          (static_cast<std::uint32_t>(buffer_[10]) << 8U) | buffer_[11],
      };
      const auto payload_length = frame.kind == yamux_frame::type::data ? frame.length_value : 0U;
      while (buffer_.size() < 12ULL + payload_length) {
         auto chunk = co_await secure_->async_read();
         buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
      }
      if (payload_length > 0) {
         frame.payload.assign(buffer_.begin() + 12,
                              buffer_.begin() + static_cast<std::ptrdiff_t>(12ULL + payload_length));
      }
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(12ULL + payload_length));
      trace_yamux("read", frame);
      co_return frame;
   }

   std::shared_ptr<relay_secure_io> secure_;
   std::uint32_t next_stream_id_ = 1;
   std::vector<std::uint8_t> buffer_;
   std::map<std::uint32_t, std::vector<std::vector<std::uint8_t>>> pending_;
   std::vector<std::uint32_t> pending_streams_;
};

boost::asio::awaitable<std::shared_ptr<yamux_session>>
upgrade_relay_outbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   const auto noise_protocol = protocol_id{.value = "/noise"};
   const auto yamux_protocol = protocol_id{.value = "/yamux/1.0.0"};
   trace_relay("outbound upgrade: select noise");
   auto noise_stream = co_await protocol_negotiation::async_select(std::move(stream), noise_protocol);
   auto secure =
       co_await noise_initiator(std::move(noise_stream), options,
                                options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("outbound upgrade: noise complete");
   if (!secure.early_yamux) {
      trace_relay("outbound upgrade: select yamux");
      (void)co_await protocol_negotiation::async_select(secure_stream(secure.secure), yamux_protocol);
   }
   auto yamux = std::make_shared<yamux_session>(std::move(secure.secure), true);
   trace_relay("outbound upgrade: yamux ready");
   co_return yamux;
}

boost::asio::awaitable<std::shared_ptr<yamux_session>>
upgrade_relay_inbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   const auto noise_protocol = protocol_id{.value = "/noise"};
   const auto yamux_protocol = protocol_id{.value = "/yamux/1.0.0"};
   trace_relay("inbound upgrade: accept noise");
   auto noise_stream = co_await protocol_negotiation::async_accept(std::move(stream), {noise_protocol});
   auto secure =
       co_await noise_responder(std::move(noise_stream.stream), options,
                                options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("inbound upgrade: noise complete");
   if (!secure.early_yamux) {
      trace_relay("inbound upgrade: accept yamux");
      (void)co_await protocol_negotiation::async_accept(secure_stream(secure.secure), {yamux_protocol});
   }
   trace_relay("inbound upgrade: yamux ready");
   co_return std::make_shared<yamux_session>(std::move(secure.secure), false);
}

boost::asio::awaitable<std::vector<std::uint8_t>>
async_read_length_delimited(fcl::p2p::stream& stream, std::vector<std::uint8_t>& buffer, std::size_t max_payload_size) {
   while (true) {
      try {
         const auto decoded = fcl::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto out = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
            co_return out;
         }
      } catch (const fcl::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            exceptions::raise(exceptions::code::codec_error, error.what());
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

[[nodiscard]] std::vector<std::uint8_t> wrap_length_delimited(std::span<const std::uint8_t> payload) {
   auto out = fcl::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> unwrap_length_delimited(std::span<const std::uint8_t> bytes,
                                                                std::size_t max_payload_size) {
   auto decoded = fcl::multiformats::decoded_varint{};
   try {
      decoded = fcl::multiformats::varint_decode(bytes);
   } catch (const fcl::multiformats::exceptions::invalid_format& error) {
      exceptions::raise(exceptions::code::codec_error, error.what());
   }
   if (decoded.value > max_payload_size) {
      exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message exceeds max size");
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (total != bytes.size()) {
      exceptions::raise(exceptions::code::codec_error, "libp2p protobuf message length mismatch");
   }
   return {bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size), bytes.end()};
}

[[nodiscard]] peer_exchange_codec::options codec_for(const node::options& options) noexcept {
   return peer_exchange_codec::options{
       .max_message_size = static_cast<std::uint32_t>(options.limits.max_peer_exchange_message_size),
       .max_endpoint_records = static_cast<std::uint32_t>(options.limits.max_peer_exchange_records),
   };
}

[[nodiscard]] fcl::quic::frame_codec_options frame_codec_for(const node::options& options) noexcept {
   return fcl::quic::frame_codec_options{
       .max_frame_size = static_cast<std::uint32_t>(options.transport_limits.max_frame_size),
   };
}

void validate_operation_timeout(std::chrono::milliseconds timeout, std::string_view name) {
   if (timeout.count() <= 0) {
      exceptions::raise(exceptions::code::invalid_options, std::string{name} + " must be positive");
   }
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point started,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view operation) {
   validate_operation_timeout(timeout, operation);
   const auto elapsed =
       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
   if (elapsed >= timeout) {
      exceptions::raise(exceptions::code::timeout, std::string{operation} + " timed out");
   }
   return timeout - elapsed;
}

[[nodiscard]] std::chrono::milliseconds
attempt_timeout(std::chrono::milliseconds remaining, std::chrono::milliseconds configured, std::string_view operation) {
   validate_operation_timeout(remaining, operation);
   validate_operation_timeout(configured, operation);
   return std::min(remaining, configured);
}

class operation_deadline {
 public:
   operation_deadline(boost::asio::io_context& context, std::chrono::milliseconds timeout)
       : timer_(std::make_shared<asio::steady_timer>(context)),
         state_(std::make_shared<std::atomic<state_value>>(state_value::pending)) {
      validate_operation_timeout(timeout, "P2P operation timeout");
      timer_->expires_after(timeout);
   }

   operation_deadline(const operation_deadline&) = delete;
   operation_deadline& operator=(const operation_deadline&) = delete;

   ~operation_deadline() {
      cancel();
   }

   template <typename Cancel> void arm(Cancel cancel) {
      auto timer = timer_;
      auto state = state_;
      timer_->async_wait([timer, state, cancel = std::move(cancel)](boost::system::error_code ec) mutable {
         if (ec) {
            return;
         }
         auto expected = state_value::pending;
         if (!state->compare_exchange_strong(expected, state_value::timed_out, std::memory_order_acq_rel)) {
            return;
         }
         cancel();
      });
   }

   [[nodiscard]] bool finish() noexcept {
      auto expected = state_value::pending;
      if (state_->compare_exchange_strong(expected, state_value::completed, std::memory_order_acq_rel)) {
         cancel();
         return true;
      }
      cancel();
      return state_->load(std::memory_order_acquire) != state_value::timed_out;
   }

   void cancel() noexcept {
      if (!timer_) {
         return;
      }
      try {
         timer_->cancel();
      } catch (...) {
         // Timer cancellation must not escape destructor/cleanup paths.
      }
   }

   [[nodiscard]] bool timed_out() const noexcept {
      return state_->load(std::memory_order_acquire) == state_value::timed_out;
   }

 private:
   enum class state_value : std::uint8_t { pending, completed, timed_out };

   std::shared_ptr<asio::steady_timer> timer_;
   std::shared_ptr<std::atomic<state_value>> state_;
};

[[noreturn]] void throw_operation_timeout(std::string_view operation) {
   exceptions::raise(exceptions::code::timeout, std::string{operation} + " timed out");
}

[[nodiscard]] std::shared_ptr<peer_store::backend> make_peer_store_backend(const node::options& options) {
   if (options.peer_store_backend) {
      return options.peer_store_backend;
   }
   if (options.peer_store_path) {
      return peer_store::make_rocksdb_backend(peer_store::rocksdb_options{.path = *options.peer_store_path});
   }
   return peer_store::make_memory_backend();
}

} // namespace

void validate(const node::options& options) {
   if (!options.allow_insecure_test_mode && (options.certificate_pem.empty() || options.private_key_pem.empty())) {
      exceptions::raise(exceptions::code::invalid_options, "production P2P node requires mTLS certificate and private key");
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "P2P certificate and private key must be provided together");
   }
   if (options.explicit_peer_id && !valid_peer_id(*options.explicit_peer_id)) {
      exceptions::raise(exceptions::code::invalid_options, "invalid explicit P2P peer id");
   }
   if (options.allow_insecure_test_mode && options.certificate_pem.empty() && !options.explicit_peer_id) {
      exceptions::raise(exceptions::code::invalid_options,
                      "insecure P2P test node without certificate requires explicit peer id");
   }
   if (options.peer_store_backend && options.peer_store_path) {
      exceptions::raise(exceptions::code::invalid_options, "P2P peer store backend and path are mutually exclusive");
   }
   if (!options.allow_insecure_test_mode && !options.peer_store_backend && !options.peer_store_path) {
      exceptions::raise(exceptions::code::invalid_options, "production P2P node requires persistent peer store path");
   }
   if (options.peer_store_path && options.peer_store_path->empty()) {
      exceptions::raise(exceptions::code::invalid_options, "P2P peer store path must not be empty");
   }
   if (options.limits.max_sessions == 0 || options.limits.max_protocol_handlers == 0 ||
       options.limits.max_peer_exchange_message_size == 0 || options.limits.max_peer_exchange_records == 0 ||
       options.limits.max_peer_exchange_queue == 0 || options.limits.relay.max_active_relays == 0 ||
       options.limits.relay.max_reservations == 0 || options.limits.relay.max_streams_per_reservation == 0 ||
       options.limits.relay.max_relay_bytes == 0 || options.limits.relay.max_queued_bytes == 0 ||
       options.limits.relay.max_duration.count() <= 0 || options.limits.relay.reservation_ttl.count() <= 0 ||
       options.limits.resources.max_streams == 0 || options.limits.resources.max_streams_per_peer == 0 ||
       options.limits.resources.max_streams_per_protocol == 0 || options.limits.resources.max_relay_reservations == 0 ||
       options.limits.resources.max_relay_streams == 0 || options.limits.resources.max_relay_bytes == 0 ||
       options.limits.resources.max_queued_bytes == 0 || options.limits.resources.max_dial_attempts_per_peer == 0 ||
       options.limits.resources.max_malformed_messages_per_peer == 0) {
      exceptions::raise(exceptions::code::invalid_options, "invalid P2P node limits");
   }
   if (!options.path_policy.allow_direct && !options.path_policy.allow_hole_punch && !options.path_policy.allow_relay) {
      exceptions::raise(exceptions::code::invalid_options, "P2P path policy must allow at least one path kind");
   }
   if (options.path_policy.max_direct_endpoints == 0 || options.path_policy.max_relay_candidates == 0) {
      exceptions::raise(exceptions::code::invalid_options, "P2P path policy limits must be positive");
   }
}

struct node::impl : std::enable_shared_from_this<impl> {
   struct session_state {
      node::session_info info;
      fcl::quic::connection connection;
      std::optional<fcl::quic::endpoint> direct_endpoint;
      bool closed = false;
   };

   struct relay_reservation_state {
      peer_id owner;
      peer_id relay_peer;
      std::uint64_t id = 0;
      std::chrono::steady_clock::time_point expires_at{};
      std::size_t max_streams = 0;
      std::uint64_t max_bytes = 0;
      std::size_t max_queued_bytes = 0;
      std::size_t active_streams = 0;
      std::uint64_t bytes = 0;
      bool canceled = false;
   };

   impl(fcl::asio::runtime& runtime_value, node::options options_value)
       : runtime(runtime_value), options(std::move(options_value)),
         local(options.explicit_peer_id ? *options.explicit_peer_id
                                        : make_peer_id_from_certificate_pem(options.certificate_pem)),
         connector(runtime_value), store(peer_store::options{.backend = make_peer_store_backend(options)}) {}

   fcl::asio::runtime& runtime;
   node::options options;
   peer_id local;
   fcl::quic::connector connector;
   std::unique_ptr<fcl::quic::listener> listener;

   mutable std::mutex mutex;
   peer_store store;
   std::map<protocol_id, node::protocol_handler> handlers;
   std::map<peer_id, std::shared_ptr<session_state>> sessions;
   std::map<peer_id, relay_reservation_state> inbound_relay_reservations;
   std::map<peer_id, relay_reservation_state> outbound_relay_reservations;
   std::map<peer_id, std::uint64_t> pending_autonat_v2_nonces;
   std::uint64_t next_reservation_id = 1;
   resource_manager resources{options.limits.resources};
   node::metrics_snapshot metrics_value;
   std::size_t active_ping_streams = 0;
   bool stopped = false;

   [[nodiscard]] std::optional<fcl::quic::endpoint> local_endpoint_for_control() const {
      auto lock = std::scoped_lock{mutex};
      if (listener) {
         return listener->local_endpoint();
      }
      if (!options.advertised_endpoints.empty()) {
         return options.advertised_endpoints.front();
      }
      return std::nullopt;
   }

   [[nodiscard]] fcl::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options.allow_insecure_test_mode) {
         auto security = fcl::quic::security_options{.verify_peer = true};
         security.verifier = [](const fcl::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = fcl::quic::security_options{.verify_peer = true};
      if (expected) {
         security.expected_sha256_fingerprint = expected->value;
      } else {
         security.verifier = [](const fcl::quic::peer_certificate& certificate) {
            return valid_peer_id(make_peer_id_from_certificate(certificate));
         };
      }
      return security;
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected) const {
      return fcl::quic::client_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected,
                                                               std::chrono::milliseconds timeout) const {
      auto out = quic_client_options(std::move(expected));
      out.connect_timeout = timeout;
      out.handshake_timeout = timeout;
      return out;
   }

   [[nodiscard]] fcl::quic::server_options quic_server_options() const {
      return fcl::quic::server_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] peer_id verified_peer_id(const fcl::quic::connection& connection,
                                          const std::optional<peer_id>& expected) const {
      if (options.allow_insecure_test_mode) {
         if (expected) {
            return *expected;
         }
         if (const auto certificate = connection.peer_certificate()) {
            return make_peer_id_from_certificate(*certificate);
         }
         return peer_id{.value = "insecure-test-peer"};
      }

      const auto certificate = connection.peer_certificate();
      if (!certificate) {
         exceptions::raise(exceptions::code::peer_verification_failed, "P2P session has no verified peer certificate");
      }
      const auto certificate_peer = make_peer_id_from_certificate(*certificate);
      if (expected && *expected != certificate_peer) {
         exceptions::raise(exceptions::code::peer_verification_failed, "P2P peer id does not match expected peer");
      }
      return certificate_peer;
   }

   void learn_from_message(const peer_exchange_message& message) {
      if (valid_peer_id(message.peer)) {
         store.upsert(peer_store::record{
             .peer = message.peer,
             .capabilities = message.capabilities,
         });
      }
      for (const auto& endpoint : message.endpoints) {
         if (valid_peer_id(endpoint.peer)) {
            store.learn_endpoint(endpoint.peer, endpoint.endpoint, endpoint.capabilities);
         }
      }
   }

   [[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(const fcl::quic::endpoint& value) const {
      return fcl::p2p::endpoint{
          .kind = fcl::p2p::endpoint::address_kind::ip4,
          .host = value.host,
          .port = value.port,
          .peer = local,
      };
   }

   [[nodiscard]] identify::document local_identify_document() const {
      auto endpoints = std::vector<fcl::p2p::endpoint>{};
      endpoints.reserve(options.advertised_endpoints.size() + 1);
      for (const auto& endpoint : options.advertised_endpoints) {
         endpoints.push_back(p2p_endpoint_for(endpoint));
      }
      if (auto endpoint = local_endpoint_for_control()) {
         endpoints.push_back(p2p_endpoint_for(*endpoint));
      }
      return identify::document{
          .protocol_version = options.protocol_version,
          .agent_version = options.agent_version,
          .public_key = options.public_key,
          .listen_endpoints = std::move(endpoints),
          .protocols = supported_protocols(),
      };
   }

   void learn_from_identify(const peer_id& peer, const identify::document& document) {
      auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
      record.protocol_version = document.protocol_version;
      record.agent_version = document.agent_version;
      record.public_key = document.public_key;
      record.protocols = document.protocols;
      record.signed_peer_record = document.signed_peer_record;
      record.observed_endpoint = document.observed_endpoint
                                     ? std::make_optional(document.observed_endpoint->quic_endpoint())
                                     : record.observed_endpoint;
      for (const auto& endpoint : document.listen_endpoints) {
         const auto quic_endpoint = endpoint.quic_endpoint();
         const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
            return current.endpoint.host == quic_endpoint.host && current.endpoint.port == quic_endpoint.port;
         });
         if (!exists) {
            record.endpoints.push_back(peer_store::endpoint_record{
                .endpoint = quic_endpoint,
                .kind = path::kind::direct,
            });
         }
      }
      store.upsert(std::move(record));
   }

   void remember_session(std::shared_ptr<session_state> session) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.size() >= options.limits.max_sessions && !sessions.contains(session->info.remote_peer)) {
         ++metrics_value.backpressure_rejections;
         exceptions::raise(exceptions::code::backpressure_rejected, "P2P max sessions reached");
      }
      sessions[session->info.remote_peer] = std::move(session);
      metrics_value.active_sessions = sessions.size();
      ++metrics_value.sessions_opened;
      ++metrics_value.handshakes_completed;
   }

   void forget_session(const peer_id& peer) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.erase(peer) != 0) {
         metrics_value.active_sessions = sessions.size();
         ++metrics_value.sessions_closed;
      }
   }

   [[nodiscard]] std::shared_ptr<session_state> session_for(const peer_id& peer) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = sessions.find(peer);
      if (it == sessions.end()) {
         return {};
      }
      return it->second;
   }

   [[nodiscard]] std::optional<node::protocol_handler> handler_for(const protocol_id& protocol) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = handlers.find(protocol);
      if (it == handlers.end()) {
         return std::nullopt;
      }
      return it->second;
   }

   [[nodiscard]] std::vector<protocol_id> supported_protocols() const {
      auto lock = std::scoped_lock{mutex};
      auto out = std::vector<protocol_id>{builtins::ping,
                                          builtins::identify,
                                          builtins::identify_push,
                                          builtins::autonat_v2_dial_request,
                                          builtins::autonat_v2_dial_back,
                                          builtins::autonat_v1,
                                          builtins::relay_stop,
                                          builtins::dcutr};
      if (options.capabilities.has(capabilities::relay) || options.capabilities.has(capabilities::relay_reservation)) {
         out.push_back(builtins::relay_hop);
      }
      out.reserve(out.size() + handlers.size());
      for (const auto& [protocol, _] : handlers) {
         out.push_back(protocol);
      }
      return out;
   }

   void remember_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
      auto lock = std::scoped_lock{mutex};
      pending_autonat_v2_nonces[peer] = nonce;
   }

   void forget_autonat_v2_nonce(const peer_id& peer) {
      auto lock = std::scoped_lock{mutex};
      pending_autonat_v2_nonces.erase(peer);
   }

   [[nodiscard]] bool consume_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
      auto lock = std::scoped_lock{mutex};
      const auto it = pending_autonat_v2_nonces.find(peer);
      if (it != pending_autonat_v2_nonces.end() && it->second == nonce) {
         pending_autonat_v2_nonces.erase(it);
         return true;
      }
      if (options.allow_insecure_test_mode) {
         const auto nonce_it =
             std::ranges::find_if(pending_autonat_v2_nonces, [&](const auto& item) { return item.second == nonce; });
         if (nonce_it != pending_autonat_v2_nonces.end()) {
            pending_autonat_v2_nonces.erase(nonce_it);
            return true;
         }
      }
      return false;
   }

   void increment_opened_protocol() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_opened;
   }

   void increment_protocol_accepted() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_accepted;
   }

   void increment_protocol_rejected() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_rejections;
   }

   void increment_peer_exchange() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.peer_exchange_messages;
   }

   [[nodiscard]] bool begin_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams >= 2) {
         ++metrics_value.backpressure_rejections;
         ++metrics_value.protocol_rejections;
         return false;
      }
      ++active_ping_streams;
      return true;
   }

   void finish_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams > 0) {
         --active_ping_streams;
      }
   }

   void increment_reachability_check(reachability::state state) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.reachability_checks;
      if (state == reachability::state::publicly_reachable) {
         ++metrics_value.reachability_public;
      } else if (state == reachability::state::private_network || state == reachability::state::blocked ||
                 state == reachability::state::relay_only) {
         ++metrics_value.reachability_private;
      }
   }

   void cleanup_expired_relay_reservations_locked() {
      const auto now = std::chrono::steady_clock::now();
      for (auto it = inbound_relay_reservations.begin(); it != inbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            if (metrics_value.active_relay_reservations > 0) {
               --metrics_value.active_relay_reservations;
            }
            resources.release_relay_reservation(
                resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
            ++metrics_value.relay_reservation_expirations;
            it = inbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
      for (auto it = outbound_relay_reservations.begin(); it != outbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            it = outbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
   }

   [[nodiscard]] bool has_outbound_relay_reservation(const peer_id& relay_peer) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      return outbound_relay_reservations.contains(relay_peer);
   }

   bool remember_outbound_relay_reservation(relay_reservation_state reservation) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      outbound_relay_reservations[reservation.relay_peer] = std::move(reservation);
      return true;
   }

   void remember_relay_reservation_in_store(const relay::reservation::info& info) {
      auto record = store.find(info.relay_peer).value_or(peer_store::record{.peer = info.relay_peer});
      auto relay_endpoints = std::vector<fcl::quic::endpoint>{};
      relay_endpoints.reserve(info.relay_endpoints.size());
      for (const auto& endpoint : info.relay_endpoints) {
         relay_endpoints.push_back(endpoint.quic_endpoint());
      }
      auto reservation = peer_store::relay_record{
          .relay = info.relay_peer,
          .reservation_id = info.id,
          .expires_at = std::chrono::system_clock::time_point{info.expires_at},
          .endpoints = std::move(relay_endpoints),
          .voucher = info.voucher ? info.voucher->encode() : std::vector<std::uint8_t>{},
      };
      const auto current = std::ranges::find_if(record.relay_reservations,
                                                [&](const auto& value) { return value.relay == info.relay_peer; });
      if (current == record.relay_reservations.end()) {
         record.relay_reservations.push_back(std::move(reservation));
      } else {
         *current = std::move(reservation);
      }
      record.capabilities.add(capabilities::relay);
      record.capabilities.add(capabilities::relay_reservation);
      store.upsert(std::move(record));
   }

   [[nodiscard]] std::optional<relay_reservation_state>
   remember_inbound_relay_reservation(const peer_id& owner, relay::reservation::options request) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (inbound_relay_reservations.size() >= options.limits.relay.max_reservations &&
          !inbound_relay_reservations.contains(owner)) {
         ++metrics_value.relay_reservation_rejections;
         return std::nullopt;
      }
      if (!inbound_relay_reservations.contains(owner) &&
          !resources.try_acquire_relay_reservation(
              resource_manager::scope{.peer = owner, .protocol = builtins::relay_hop})) {
         ++metrics_value.relay_reservation_rejections;
         return std::nullopt;
      }
      const auto ttl = std::min(request.ttl, options.limits.relay.reservation_ttl);
      auto reservation = relay_reservation_state{
          .owner = owner,
          .relay_peer = local,
          .id = next_reservation_id++,
          .expires_at = std::chrono::steady_clock::now() + ttl,
          .max_streams = std::min(request.max_streams, options.limits.relay.max_streams_per_reservation),
          .max_bytes = std::min(request.max_bytes, options.limits.relay.max_relay_bytes),
          .max_queued_bytes = std::min(request.max_queued_bytes, options.limits.relay.max_queued_bytes),
      };
      inbound_relay_reservations[owner] = reservation;
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      ++metrics_value.relay_reservations;
      return reservation;
   }

   bool cancel_inbound_relay_reservation(const peer_id& owner, std::uint64_t reservation_id) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      const auto it = inbound_relay_reservations.find(owner);
      if (it == inbound_relay_reservations.end() || (reservation_id != 0 && it->second.id != reservation_id)) {
         return false;
      }
      resources.release_relay_reservation(
          resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
      inbound_relay_reservations.erase(it);
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      return true;
   }

   relay::status begin_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (metrics_value.active_relays >= options.limits.relay.max_active_relays ||
          !resources.try_acquire_relay_stream()) {
         ++metrics_value.relay_rejections;
         return relay::status::resource_limit_exceeded;
      }
      if (options.limits.relay.require_reservation) {
         const auto reservation = inbound_relay_reservations.find(owner);
         if (reservation == inbound_relay_reservations.end()) {
            resources.release_relay_stream();
            ++metrics_value.relay_rejections;
            return relay::status::no_reservation;
         }
         if (reservation->second.active_streams >= reservation->second.max_streams ||
             reservation->second.bytes >= reservation->second.max_bytes) {
            resources.release_relay_stream();
            ++metrics_value.relay_rejections;
            return relay::status::resource_limit_exceeded;
         }
         ++reservation->second.active_streams;
      }
      ++metrics_value.active_relays;
      ++metrics_value.relays_opened;
      return relay::status::ok;
   }

   [[nodiscard]] std::uint64_t relay_byte_limit(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      const auto reservation = inbound_relay_reservations.find(owner);
      if (reservation != inbound_relay_reservations.end()) {
         return reservation->second.max_bytes;
      }
      return options.limits.relay.max_relay_bytes;
   }

   void finish_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation != inbound_relay_reservations.end() && reservation->second.active_streams > 0) {
         --reservation->second.active_streams;
      }
      if (metrics_value.active_relays > 0) {
         --metrics_value.active_relays;
      }
      resources.release_relay_stream();
   }

   bool add_relay_bytes(const peer_id& owner, std::uint64_t bytes) {
      auto lock = std::scoped_lock{mutex};
      if (!resources.add_relay_bytes(bytes)) {
         ++metrics_value.relay_rejections;
         return false;
      }
      metrics_value.relay_bytes += bytes;
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation == inbound_relay_reservations.end()) {
         return !options.limits.relay.require_reservation;
      }
      if (reservation->second.bytes + bytes > reservation->second.max_bytes) {
         ++metrics_value.relay_rejections;
         return false;
      }
      reservation->second.bytes += bytes;
      return true;
   }

   void record_path_open(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_opens;
      } else {
         ++metrics_value.path_relay_opens;
      }
   }

   void record_path_attempt(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_attempts;
      } else {
         ++metrics_value.path_relay_attempts;
      }
   }

   void record_hole_punch_result(hole_punch::status status) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.hole_punch_attempts;
      if (status == hole_punch::status::succeeded) {
         ++metrics_value.hole_punch_successes;
      } else if (status == hole_punch::status::failed) {
         ++metrics_value.hole_punch_failures;
      }
   }

   void record_direct_failure(const peer_id& peer) {
      store.mark_failure(peer);
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.direct_failures;
   }

   void record_relay_failure() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.relay_failures;
   }

   boost::asio::awaitable<std::shared_ptr<session_state>> connect_direct(fcl::quic::endpoint endpoint,
                                                                         node::connect_options connect_options_value) {
      validate_operation_timeout(connect_options_value.timeout, "P2P connect timeout");
      auto deadline = std::unique_ptr<operation_deadline>{};
      auto endpoint_copy = endpoint;
      try {
         auto started = std::chrono::steady_clock::now();
         auto connection = std::make_shared<fcl::quic::connection>(co_await connector.async_connect(
             std::move(endpoint),
             quic_client_options(connect_options_value.expected_peer, connect_options_value.timeout)));
         deadline = std::make_unique<operation_deadline>(
             runtime.context(), remaining_timeout(started, connect_options_value.timeout, "P2P connect"));
         deadline->arm([connection] { connection->cancel(); });
         if (!deadline->finish()) {
            throw_operation_timeout("P2P connect");
         }
         const auto remote = verified_peer_id(*connection, connect_options_value.expected_peer);
         store.mark_endpoint_success(
             remote, endpoint_copy, path::kind::direct,
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started));
         auto session = std::make_shared<session_state>(session_state{
             .info = node::session_info{.remote_peer = remote,
                                        .capabilities = options.capabilities,
                                        .path = path::kind::direct},
             .connection = std::move(*connection),
             .direct_endpoint = endpoint_copy,
         });
         remember_session(session);
         launch_session_accept_loop(session);
         co_return session;
      } catch (const fcl::exception::base& error) {
         if (deadline && deadline->timed_out()) {
            throw_operation_timeout("P2P connect");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<std::shared_ptr<session_state>> ensure_direct_session(
       const peer_id& peer, std::chrono::milliseconds timeout = node::connect_options{}.timeout,
       std::size_t max_direct_endpoints = node::connect_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::connect_options{}.direct_attempt_timeout) {
      if (auto existing = session_for(peer)) {
         co_return existing;
      }
      const auto record = store.find(peer);
      if (!record || record->endpoints.empty()) {
         exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no known direct endpoint");
      }
      if (max_direct_endpoints == 0) {
         exceptions::raise(exceptions::code::invalid_options, "P2P max direct endpoints must be positive");
      }
      auto endpoints = record->endpoints;
      const auto now = std::chrono::system_clock::now();
      auto preferred = std::vector<peer_store::endpoint_record>{};
      for (const auto& endpoint : endpoints) {
         if (endpoint.kind != path::kind::direct || endpoint.relay_peer) {
            continue;
         }
         if (endpoint.backoff_until != std::chrono::system_clock::time_point{} && endpoint.backoff_until > now) {
            continue;
         }
         preferred.push_back(endpoint);
      }
      if (preferred.empty()) {
         for (const auto& endpoint : endpoints) {
            if (endpoint.kind == path::kind::direct && !endpoint.relay_peer) {
               preferred.push_back(endpoint);
            }
         }
      }
      std::stable_sort(preferred.begin(), preferred.end(),
                       [](const auto& left, const auto& right) { return left.score > right.score; });

      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<exceptions::code>{};
      auto last_message = std::string{};
      const auto attempts = std::min(max_direct_endpoints, preferred.size());
      for (std::size_t index = 0; index < attempts; ++index) {
         const auto remaining = remaining_timeout(started, timeout, "P2P direct path");
         const auto per_attempt = attempt_timeout(remaining, direct_attempt_timeout, "P2P direct path attempt");
         const auto endpoint = preferred[index].endpoint;
         record_path_attempt(path::kind::direct);
         try {
            co_return co_await connect_direct(
                endpoint, node::connect_options{.expected_peer = peer, .allow_relay = false, .timeout = per_attempt});
         } catch (const fcl::exception::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
            store.mark_endpoint_failure(peer, endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{5});
            record_direct_failure(peer);
         }
      }
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no direct endpoint outside backoff");
   }

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_direct(
       const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
       std::size_t max_direct_endpoints = node::open_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::open_options{}.direct_attempt_timeout) {
      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<exceptions::code>{};
      auto last_message = std::string{};
      for (std::size_t attempt = 0; attempt < max_direct_endpoints; ++attempt) {
         const auto remaining = remaining_timeout(started, timeout, "P2P protocol open");
         auto session = co_await ensure_direct_session(peer, remaining, max_direct_endpoints, direct_attempt_timeout);
         auto deadline = operation_deadline{
             runtime.context(), attempt_timeout(remaining, direct_attempt_timeout, "P2P protocol open direct attempt")};
         deadline.arm([session] { session->connection.cancel(); });
         record_path_attempt(path::kind::direct);
         try {
            auto selected =
                co_await protocol_negotiation::async_select(co_await session->connection.async_open_stream(), protocol);
            if (!deadline.finish()) {
               throw_operation_timeout("P2P protocol open");
            }
            increment_opened_protocol();
            record_path_open(path::kind::direct);
            co_return selected;
         } catch (const fcl::exception::base& error) {
            if (!deadline.finish() || deadline.timed_out()) {
               session->closed = true;
               forget_session(peer);
               if (session->direct_endpoint) {
                  store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                              std::chrono::system_clock::now() + std::chrono::seconds{5});
               }
               record_direct_failure(peer);
               last_kind = exceptions::code::timeout;
               last_message = "P2P protocol open timed out";
               continue;
            }
            const auto p2p_kind = exceptions::code_of(error);
            if (p2p_kind == exceptions::code::unsupported_protocol || p2p_kind == exceptions::code::protocol_error ||
                p2p_kind == exceptions::code::codec_error) {
               throw;
            }
            session->closed = true;
            forget_session(peer);
            if (session->direct_endpoint) {
               store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                           std::chrono::system_clock::now() + std::chrono::seconds{5});
            }
            record_direct_failure(peer);
            last_kind = p2p_kind ? *p2p_kind : map_quic_error(quic_code(error));
            last_message = error.what();
            continue;
         }
      }
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::peer_not_found, "P2P direct path attempts were exhausted");
   }

   boost::asio::awaitable<relay::reservation::info>
   request_relay_reservation(const peer_id& relay_peer, relay::reservation::options reservation_options,
                             std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P relay reservation timeout");
      if (!options.relay_policy.client_enabled) {
         exceptions::raise(exceptions::code::relay_not_available, "P2P relay client policy is disabled");
      }
      if (reservation_options.ttl.count() <= 0 || reservation_options.max_streams == 0 ||
          reservation_options.max_bytes == 0 || reservation_options.max_queued_bytes == 0) {
         exceptions::raise(exceptions::code::invalid_options, "invalid P2P relay reservation options");
      }
      const auto started = std::chrono::steady_clock::now();
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay reservation")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto stream = co_await protocol_negotiation::async_select(
             co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
         co_await stream.async_write(
             relay::codec::encode_hop(relay::hop_message{.kind = relay::hop_message::message_kind::reserve}));
         auto relay_buffer = std::vector<std::uint8_t>{};
         auto response = relay::codec::decode_hop(
             co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay reservation");
         }
         if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok ||
             !response.reservation_value) {
            exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                      : exceptions::code::protocol_error,
                            "P2P relay reservation rejected");
         }
         const auto now_seconds =
             std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
         const auto expires_at =
             std::chrono::seconds{static_cast<std::int64_t>(response.reservation_value->expires_at)};
         const auto ttl = expires_at > now_seconds ? expires_at - now_seconds : std::chrono::seconds{1};
         const auto limit = response.limit_value.value_or(relay::limit{
             .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
             .data = options.limits.relay.max_relay_bytes,
         });
         auto info = relay::reservation::info{
             .relay_peer = relay_peer,
             .id = response.reservation_value->expires_at,
             .expires_at = expires_at,
             .ttl = std::chrono::duration_cast<std::chrono::milliseconds>(ttl),
             .max_streams = reservation_options.max_streams,
             .max_bytes = limit.data == 0 ? reservation_options.max_bytes : limit.data,
             .max_queued_bytes = reservation_options.max_queued_bytes,
             .relay_endpoints = response.reservation_value->relay_endpoints,
             .voucher = response.reservation_value->voucher,
         };
         // libp2p Circuit Relay v2 vouchers are signed envelopes. Keep the
         // envelope bytes intact here; validation belongs to the signed-envelope
         // layer, not to the older FCL-local voucher shape.
         remember_outbound_relay_reservation(relay_reservation_state{
             .owner = local,
             .relay_peer = relay_peer,
             .id = info.id,
             .expires_at = std::chrono::steady_clock::now() + info.ttl,
             .max_streams = info.max_streams,
             .max_bytes = info.max_bytes,
             .max_queued_bytes = info.max_queued_bytes,
         });
         remember_relay_reservation_in_store(info);
         co_return info;
      } catch (const fcl::exception::base& error) {
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay reservation");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<void> ensure_relay_reservation(const peer_id& relay_peer, std::chrono::milliseconds timeout) {
      if (has_outbound_relay_reservation(relay_peer)) {
         co_return;
      }
      (void)co_await request_relay_reservation(relay_peer,
                                               relay::reservation::options{
                                                   .ttl = options.limits.relay.reservation_ttl,
                                                   .max_streams = options.limits.relay.max_streams_per_reservation,
                                                   .max_bytes = options.limits.relay.max_relay_bytes,
                                                   .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                               },
                                               timeout);
   }

   boost::asio::awaitable<std::shared_ptr<yamux_session>>
   open_relay_yamux(const peer_id& peer, const peer_id& relay_peer, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      record_path_attempt(path::kind::relay);
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay protocol open")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto stream = co_await protocol_negotiation::async_select(
             co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::connect,
             .target = relay::peer{.id = peer},
         }));
         auto relay_buffer = std::vector<std::uint8_t>{};
         auto response = relay::codec::decode_hop(
             co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay protocol open");
         }
         if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok) {
            exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                      : exceptions::code::protocol_error,
                            response.kind == relay::hop_message::message_kind::status
                                ? "P2P relay open rejected with status " +
                                      std::to_string(static_cast<std::uint16_t>(response.status))
                                : "P2P relay open rejected with unexpected response");
         }
         record_path_open(path::kind::relay);
         stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
         co_return co_await upgrade_relay_outbound_session(std::move(stream), options, peer);
      } catch (const fcl::exception::base& error) {
         record_relay_failure();
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay protocol open");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_via_relay(const peer_id& peer, const protocol_id& protocol,
                                                                    const peer_id& relay_peer,
                                                                    std::chrono::milliseconds timeout) {
      auto yamux = co_await open_relay_yamux(peer, relay_peer, timeout);
      trace_relay("outbound upgrade: open yamux stream");
      auto substream = co_await yamux->async_open_stream();
      auto selected = co_await protocol_negotiation::async_select(std::move(substream), protocol);
      co_return selected;
   }

   boost::asio::awaitable<void> request_peer_exchange(const peer_id& peer) {
      auto session = co_await ensure_direct_session(peer);
      try {
         auto framed = fcl::quic::framed_stream{
             co_await session->connection.async_open_stream(),
             frame_codec_for(options),
         };
         co_await peer_exchange_codec::async_write(framed,
                                                   peer_exchange_message{
                                                       .kind = peer_exchange_message::type::peer_exchange_request,
                                                       .peer = local,
                                                   },
                                                   codec_for(options));
         auto response = co_await peer_exchange_codec::async_read(framed, codec_for(options));
         if (response.kind != peer_exchange_message::type::peer_exchange_response) {
            exceptions::raise(exceptions::code::protocol_error, "P2P peer exchange expected response");
         }
         learn_from_message(response);
         increment_peer_exchange();
      } catch (const fcl::exception::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   void launch_accept_loop() {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self]() -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || !self->listener) {
                      co_return;
                   }
                }
                try {
                   auto connection = co_await self->listener->async_accept();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, connection = std::move(connection)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_inbound_connection(std::move(connection));
                       },
                       asio::detached);
                } catch (const std::exception&) {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped) {
                      co_return;
                   }
                   ++self->metrics_value.handshakes_failed;
                } catch (...) {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped) {
                      co_return;
                   }
                   ++self->metrics_value.handshakes_failed;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_inbound_connection(fcl::quic::connection connection) {
      try {
         const auto remote = verified_peer_id(connection, std::nullopt);
         auto session = std::make_shared<session_state>(session_state{
             .info = node::session_info{.remote_peer = remote,
                                        .capabilities = options.capabilities,
                                        .path = path::kind::direct},
             .connection = std::move(connection),
         });
         remember_session(session);
         launch_session_accept_loop(session);
      } catch (const std::exception&) {
         // The listener owns detached accepts; failed handshakes are reflected in metrics.
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.handshakes_failed;
      } catch (...) {
         // The listener owns detached accepts; failed handshakes are reflected in metrics.
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.handshakes_failed;
      }
      co_return;
   }

   void launch_session_accept_loop(std::shared_ptr<session_state> session) {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self, session = std::move(session)]() mutable -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || session->closed) {
                      co_return;
                   }
                }
                try {
                   auto stream = co_await session->connection.async_accept_stream();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, session, stream = std::move(stream)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_incoming_stream(session, std::move(stream));
                       },
                       asio::detached);
                } catch (...) {
                   session->closed = true;
                   self->forget_session(session->info.remote_peer);
                   co_return;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_incoming_stream(std::shared_ptr<session_state> session, fcl::quic::stream raw) {
      try {
         auto negotiated = co_await protocol_negotiation::async_accept(std::move(raw), supported_protocols());
         if (negotiated.protocol == builtins::ping) {
            co_await handle_ping(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::identify) {
            co_await handle_identify(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::identify_push) {
            co_await handle_identify_push(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::autonat_v2_dial_request) {
            co_await handle_autonat_v2_dial_request(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::autonat_v2_dial_back) {
            co_await handle_autonat_v2_dial_back(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::autonat_v1) {
            co_await handle_autonat_v1(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::relay_hop) {
            co_await handle_relay_hop(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::relay_stop) {
            co_await handle_relay_stop(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::dcutr) {
            co_await handle_dcutr(session, std::move(negotiated.stream));
            co_return;
         }
         auto handler = handler_for(negotiated.protocol);
         if (!handler) {
            increment_protocol_rejected();
            exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated P2P protocol");
         }
         increment_protocol_accepted();
         co_await (*handler)(node::incoming_protocol_stream{
             .session = session->info,
             .protocol = negotiated.protocol,
             .stream = std::move(negotiated.stream),
         });
      } catch (const std::exception&) {
         increment_protocol_rejected();
      } catch (...) {
         increment_protocol_rejected();
      }
   }

   boost::asio::awaitable<void> handle_ping(fcl::p2p::stream stream) {
      if (!begin_ping_stream()) {
         exceptions::raise(exceptions::code::backpressure_rejected, "libp2p ping inbound stream limit reached");
      }
      try {
         while (true) {
            auto payload = co_await stream.async_read();
            if (payload.size() != 32) {
               exceptions::raise(exceptions::code::protocol_error, "libp2p ping payload must be 32 bytes");
            }
            co_await stream.async_write(payload);
         }
      } catch (const fcl::exception::base& error) {
         finish_ping_stream();
         if (!is_orderly_stream_close(error)) {
            throw;
         }
         co_return;
      } catch (...) {
         finish_ping_stream();
         throw;
      }
      finish_ping_stream();
   }

   boost::asio::awaitable<void> handle_identify(fcl::p2p::stream stream) {
      auto encoded = identify::encode(local_identify_document());
      co_await stream.async_write(wrap_length_delimited(encoded));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_identify_push(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto payload = unwrap_length_delimited(
          co_await async_read_length_delimited(stream, buffer, options.limits.max_peer_exchange_message_size),
          options.limits.max_peer_exchange_message_size);
      learn_from_identify(session->info.remote_peer, identify::decode(payload));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v2_dial_back(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto request = reachability::codec::decode_v2_dial_back(
          co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
      if (request.nonce == 0 || !consume_autonat_v2_nonce(session->info.remote_peer, request.nonce)) {
         exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 dial-back nonce mismatch");
      }
      co_await stream.async_write(reachability::codec::encode_v2_dial_back_response(
          reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v2_dial_request(std::shared_ptr<session_state> session,
                                                               fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto request = reachability::codec::decode_v2(
          co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
      auto response = reachability::v2::dial_response{
          .status = reachability::v2::response_status::request_rejected,
          .index = 0,
          .dial_status = reachability::v2::dial_status::unused,
      };
      if (request.type == reachability::v2::message::kind::dial_request && request.dial_request &&
          !request.dial_request->endpoints.empty() && request.dial_request->nonce != 0) {
         response.status = reachability::v2::response_status::dial_refused;
         response.dial_status = reachability::v2::dial_status::dial_error;
         const auto limit = std::min<std::uint64_t>(4096, reachability::options{}.max_data_response_size);
         for (std::size_t index = 0; index < request.dial_request->endpoints.size(); ++index) {
            const auto& candidate = request.dial_request->endpoints[index];
            co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
                .type = reachability::v2::message::kind::dial_data_request,
                .dial_data_request =
                    reachability::v2::dial_data_request{
                        .index = static_cast<std::uint32_t>(index),
                        .bytes = limit,
                    },
            }));
            const auto data = reachability::codec::decode_v2(
                co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
            if (data.type != reachability::v2::message::kind::dial_data_response || !data.dial_data_response ||
                data.dial_data_response->data.size() < limit) {
               response.status = reachability::v2::response_status::request_rejected;
               response.dial_status = reachability::v2::dial_status::dial_error;
               break;
            }
            response.index = static_cast<std::uint32_t>(index);
            try {
               auto dialed =
                   co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                          .expected_peer = session->info.remote_peer,
                                                                          .allow_relay = false,
                                                                          .timeout = std::chrono::milliseconds{1'500},
                                                                      });
               try {
                  auto dial_back = co_await protocol_negotiation::async_select(
                      co_await dialed->connection.async_open_stream(), builtins::autonat_v2_dial_back);
                  co_await dial_back.async_write(reachability::codec::encode_v2_dial_back(
                      reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
                  auto dial_back_buffer = std::vector<std::uint8_t>{};
                  const auto dial_back_response =
                      reachability::codec::decode_v2_dial_back_response(co_await async_read_length_delimited(
                          dial_back, dial_back_buffer, reachability::options{}.max_message_size));
                  if (dial_back_response.status == reachability::v2::dial_back_status::ok) {
                     response.status = reachability::v2::response_status::ok;
                     response.dial_status = reachability::v2::dial_status::ok;
                     break;
                  }
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::dial_back_error;
               } catch (...) {
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::dial_back_error;
               }
            } catch (const fcl::exception::base& error) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = p2p_code(error) == exceptions::code::peer_verification_failed
                                          ? reachability::v2::dial_status::dial_back_error
                                          : reachability::v2::dial_status::dial_error;
            } catch (...) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_error;
            }
         }
      }
      co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
          .type = reachability::v2::message::kind::dial_response,
          .dial_response = std::move(response),
      }));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v1(fcl::p2p::stream stream) {
      auto request = reachability::codec::decode_v1(co_await stream.async_read());
      auto response = reachability::dial_response{
          .status = reachability::dial_status::bad_request,
          .status_text = "expected AutoNAT dial request",
      };
      if (request.kind == reachability::message::message_kind::dial && request.peer &&
          !request.peer->endpoints.empty()) {
         response.status = reachability::dial_status::dial_error;
         response.status_text = "dial failed";
         for (const auto& candidate : request.peer->endpoints) {
            try {
               auto session =
                   co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                          .expected_peer = request.peer->peer,
                                                                          .allow_relay = false,
                                                                          .timeout = std::chrono::milliseconds{1'500},
                                                                      });
               session->closed = true;
               forget_session(request.peer->peer);
               try {
                  co_await session->connection.async_close();
               } catch (...) {
                  session->connection.cancel();
               }
               response.status = reachability::dial_status::ok;
               response.status_text.clear();
               response.endpoint = candidate;
               break;
            } catch (const fcl::exception::base& error) {
               response.status = p2p_code(error) == exceptions::code::peer_verification_failed
                                     ? reachability::dial_status::dial_refused
                                     : reachability::dial_status::dial_error;
            } catch (...) {
               response.status = reachability::dial_status::dial_error;
            }
         }
      }
      co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
          .kind = reachability::message::message_kind::dial_response,
          .response = std::move(response),
      }));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_relayed_yamux_stream(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream) {
      auto negotiated = co_await protocol_negotiation::async_accept(std::move(stream), supported_protocols());
      trace_relay(std::string{"relayed yamux: negotiated "} + negotiated.protocol.value);
      if (negotiated.protocol == builtins::ping) {
         co_await handle_ping(std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::identify) {
         co_await handle_identify(std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::identify_push) {
         co_await handle_identify_push(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::dcutr) {
         co_await handle_dcutr(session, std::move(negotiated.stream));
         co_return;
      }
      auto handler = handler_for(negotiated.protocol);
      if (!handler) {
         increment_protocol_rejected();
         exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated relayed P2P protocol");
      }
      increment_protocol_accepted();
      co_await (*handler)(node::incoming_protocol_stream{
          .session = session->info,
          .protocol = negotiated.protocol,
          .stream = std::move(negotiated.stream),
      });
   }

   boost::asio::awaitable<void> handle_relay_stop(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto request = relay::codec::decode_stop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      trace_relay("stop: request decoded");
      if (request.kind != relay::stop_message::message_kind::connect || !request.source) {
         co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
             .kind = relay::stop_message::message_kind::status,
             .status = relay::status::malformed_message,
         }));
         co_return;
      }
      co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
          .kind = relay::stop_message::message_kind::status,
          .limit_value = request.limit_value,
          .status = relay::status::ok,
      }));
      trace_relay("stop: ok sent");

      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      auto yamux = co_await upgrade_relay_inbound_session(std::move(stream), options, request.source->id);
      auto dcutr_started = false;
      while (true) {
         trace_relay("stop: accepting yamux stream");
         auto relayed_stream = co_await yamux->async_accept_stream();
         auto relayed = session->info;
         relayed.remote_peer = request.source->id;
         relayed.path = path::kind::relay;
         relayed.relay_peer = session->info.remote_peer;
         auto relayed_session = std::make_shared<session_state>();
         relayed_session->info = std::move(relayed);
         co_await handle_relayed_yamux_stream(relayed_session, std::move(relayed_stream));
         if (!dcutr_started && options.capabilities.has(capabilities::hole_punching)) {
            dcutr_started = true;
            const auto dcutr_status =
                co_await run_dcutr_initiator(request.source->id, yamux, std::chrono::milliseconds{10'000});
            trace_relay(std::string{"stop: dcutr initiator status="} + std::to_string(static_cast<int>(dcutr_status)));
         }
      }
   }

   boost::asio::awaitable<void> handle_relay_hop(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto request = relay::codec::decode_hop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      trace_relay("hop: request decoded");
      if (request.kind == relay::hop_message::message_kind::reserve) {
         if (!options.relay_policy.service_enabled || !options.capabilities.has(capabilities::relay) ||
             !options.capabilities.has(capabilities::relay_reservation)) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::permission_denied,
            }));
            co_return;
         }
         if (session->info.path == path::kind::relay) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::permission_denied,
            }));
            co_return;
         }
         auto reservation = remember_inbound_relay_reservation(
             session->info.remote_peer, relay::reservation::options{
                                            .ttl = options.limits.relay.reservation_ttl,
                                            .max_streams = options.limits.relay.max_streams_per_reservation,
                                            .max_bytes = options.limits.relay.max_relay_bytes,
                                            .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                        });
         if (!reservation) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::reservation_refused,
            }));
            co_return;
         }
         auto endpoints = std::vector<endpoint>{};
         if (auto current = local_endpoint_for_control()) {
            endpoints.push_back(p2p_endpoint_for(*current));
         }
         const auto expires_at = std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch() + options.limits.relay.reservation_ttl);
         auto voucher = std::optional<signed_envelope>{};
         if (!options.public_key.empty()) {
            const auto private_key = private_key_from_pem(options.private_key_pem);
            voucher = relay::codec::seal_reservation_voucher(
                relay::voucher{
                    .relay_peer = local,
                    .peer = session->info.remote_peer,
                    .expires_at = static_cast<std::uint64_t>(expires_at.count()),
                },
                decode_public_key(options.public_key), private_key);
         }
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .reservation_value =
                 relay::reservation{
                     .expires_at = static_cast<std::uint64_t>(expires_at.count()),
                     .relay_endpoints = std::move(endpoints),
                     .voucher = std::move(voucher),
                 },
             .limit_value =
                 relay::limit{
                     .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                     .data = options.limits.relay.max_relay_bytes,
                 },
             .status = relay::status::ok,
         }));
         trace_relay("hop: reserve ok sent");
         co_await stream.async_close();
         co_return;
      }

      if (request.kind != relay::hop_message::message_kind::connect || !request.target) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::malformed_message,
         }));
         co_return;
      }
      if (!options.relay_policy.service_enabled) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::permission_denied,
         }));
         co_return;
      }
      const auto relay_owner = request.target->id;
      const auto relay_status = begin_relay(relay_owner);
      trace_relay("hop: connect begin");
      if (relay_status != relay::status::ok) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay_status,
         }));
         co_return;
      }

      auto target = std::optional<fcl::p2p::stream>{};
      try {
         auto target_session = co_await ensure_direct_session(request.target->id);
         target.emplace(co_await protocol_negotiation::async_select(
             co_await target_session->connection.async_open_stream(), builtins::relay_stop));
         trace_relay("hop: stop selected");
         co_await target->async_write(relay::codec::encode_stop(relay::stop_message{
             .kind = relay::stop_message::message_kind::connect,
             .source = relay::peer{.id = session->info.remote_peer},
             .limit_value =
                 relay::limit{
                     .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                     .data = options.limits.relay.max_relay_bytes,
                 },
         }));
         auto stop_buffer = std::vector<std::uint8_t>{};
         const auto stop_status = relay::codec::decode_stop(
             co_await async_read_length_delimited(*target, stop_buffer, reachability::options{}.max_message_size));
         trace_relay("hop: stop status decoded");
         if (stop_status.kind != relay::stop_message::message_kind::status || stop_status.status != relay::status::ok) {
            target.reset();
         }
      } catch (...) {
         target.reset();
      }
      if (!target) {
         finish_relay(relay_owner);
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::connection_failed,
         }));
         co_return;
      }

      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .limit_value =
              relay::limit{
                  .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                  .data = options.limits.relay.max_relay_bytes,
              },
          .status = relay::status::ok,
      }));
      trace_relay("hop: connect ok sent, starting pumps");
      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      launch_relay_pumps(relay_owner, std::move(stream), std::move(*target));
   }

   boost::asio::awaitable<void> handle_dcutr(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      trace_relay("dcutr: waiting connect");
      auto buffer = std::vector<std::uint8_t>{};
      auto first = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
      trace_relay(std::string{"dcutr: connect bytes="} + std::to_string(first.size()));
      auto request = hole_punch::codec::decode(first);
      if (request.kind != hole_punch::message::message_kind::connect) {
         exceptions::raise(exceptions::code::protocol_error, "DCUtR expected CONNECT");
      }
      auto observed = std::vector<endpoint>{};
      if (auto endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*endpoint));
      }
      co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
          .kind = hole_punch::message::message_kind::connect,
          .observed_endpoints = std::move(observed),
      }));
      trace_relay("dcutr: connect sent, waiting sync");
      auto sync_bytes = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
      trace_relay(std::string{"dcutr: sync bytes="} + std::to_string(sync_bytes.size()));
      auto sync = hole_punch::codec::decode(sync_bytes);
      if (sync.kind != hole_punch::message::message_kind::sync) {
         exceptions::raise(exceptions::code::protocol_error, "DCUtR expected SYNC");
      }
      for (const auto& candidate : request.observed_endpoints) {
         trace_relay(std::string{"dcutr inbound: direct candidate "} + candidate.to_string());
         try {
            (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                         .expected_peer = session->info.remote_peer,
                                                                         .allow_relay = false,
                                                                         .timeout = std::chrono::milliseconds{5'000},
                                                                     });
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return;
         } catch (const std::exception& error) {
            trace_relay(std::string{"dcutr inbound: direct failed "} + error.what());
            record_direct_failure(session->info.remote_peer);
         } catch (...) {
            trace_relay("dcutr inbound: direct failed");
            record_direct_failure(session->info.remote_peer);
         }
      }
      if (co_await wait_for_direct_session(session->info.remote_peer, std::chrono::milliseconds{5'000})) {
         record_hole_punch_result(hole_punch::status::succeeded);
         co_return;
      }
      record_hole_punch_result(hole_punch::status::failed);
   }

   boost::asio::awaitable<bool> wait_for_direct_session(const peer_id& peer, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - started < timeout) {
         if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
            co_return true;
         }
         auto remaining = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started);
         if (remaining <= std::chrono::milliseconds{0}) {
            break;
         }
         auto timer = asio::steady_timer{runtime.context()};
         timer.expires_after(std::min(remaining, std::chrono::milliseconds{50}));
         co_await timer.async_wait(asio::use_awaitable);
      }
      co_return false;
   }

   boost::asio::awaitable<hole_punch::status>
   run_dcutr_initiator(const peer_id& peer, std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
      auto observed = std::vector<endpoint>{};
      if (auto local_endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*local_endpoint));
      }
      if (observed.empty()) {
         record_hole_punch_result(hole_punch::status::failed);
         co_return hole_punch::status::failed;
      }
      try {
         trace_relay("dcutr initiator: open yamux stream");
         auto stream = co_await yamux->async_open_stream();
         stream = co_await protocol_negotiation::async_select(std::move(stream), builtins::dcutr);
         const auto sent = std::chrono::steady_clock::now();
         co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
             .kind = hole_punch::message::message_kind::connect,
             .observed_endpoints = observed,
         }));
         auto dcutr_buffer = std::vector<std::uint8_t>{};
         auto response = hole_punch::codec::decode(
             co_await async_read_length_delimited(stream, dcutr_buffer, hole_punch::options{}.max_message_size));
         const auto rtt =
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sent);
         trace_relay(std::string{"dcutr initiator: response endpoints="} +
                     std::to_string(response.observed_endpoints.size()));
         if (response.kind != hole_punch::message::message_kind::connect || response.observed_endpoints.empty()) {
            record_hole_punch_result(hole_punch::status::failed);
            co_return hole_punch::status::failed;
         }
         co_await stream.async_write(
             hole_punch::codec::encode(hole_punch::message{.kind = hole_punch::message::message_kind::sync}));
         if (rtt > std::chrono::milliseconds{0}) {
            auto timer = asio::steady_timer{runtime.context()};
            timer.expires_after(rtt / 2);
            co_await timer.async_wait(asio::use_awaitable);
         }
         for (const auto& candidate : response.observed_endpoints) {
            trace_relay(std::string{"dcutr initiator: direct candidate "} + candidate.to_string());
            try {
               (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                            .expected_peer = peer,
                                                                            .allow_relay = false,
                                                                            .timeout = timeout,
                                                                        });
               record_hole_punch_result(hole_punch::status::succeeded);
               co_return hole_punch::status::succeeded;
            } catch (const std::exception& error) {
               trace_relay(std::string{"dcutr initiator: direct failed "} + error.what());
               record_direct_failure(peer);
            } catch (...) {
               trace_relay("dcutr initiator: direct failed");
               record_direct_failure(peer);
            }
         }
         if (co_await wait_for_direct_session(peer, std::min(timeout, std::chrono::milliseconds{5'000}))) {
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return hole_punch::status::succeeded;
         }
      } catch (const std::exception& error) {
         trace_relay(std::string{"dcutr initiator: failed "} + error.what());
      } catch (...) {
         trace_relay("dcutr initiator: failed");
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }

   boost::asio::awaitable<hole_punch::status>
   serve_relayed_streams_until_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                          std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      for (auto handled = 0U; handled != 8U; ++handled) {
         if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return hole_punch::status::succeeded;
         }
         const auto elapsed =
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
         if (elapsed >= timeout) {
            break;
         }
         auto before = std::uint64_t{0};
         {
            auto lock = std::scoped_lock{mutex};
            before = metrics_value.hole_punch_successes;
         }
         auto relayed_session = std::make_shared<session_state>();
         relayed_session->info = node::session_info{
             .remote_peer = peer,
             .capabilities = capability_set{.bits = capabilities::hole_punching},
             .path = path::kind::relay,
             .relay_peer = relay_peer,
         };
         auto incoming = co_await yamux->async_accept_stream();
         auto negotiated = co_await protocol_negotiation::async_accept(std::move(incoming), supported_protocols());
         trace_relay(std::string{"relayed yamux wait: negotiated "} + negotiated.protocol.value);
         if (negotiated.protocol == builtins::dcutr) {
            co_await handle_dcutr(relayed_session, std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::identify) {
            co_await handle_identify(std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::identify_push) {
            co_await handle_identify_push(relayed_session, std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::ping) {
            auto self = shared_from_this();
            asio::co_spawn(
                runtime.context(),
                [self, stream = std::move(negotiated.stream)]() mutable -> asio::awaitable<void> {
                   try {
                      co_await self->handle_ping(std::move(stream));
                   } catch (...) {
                      self->increment_protocol_rejected();
                   }
                },
                asio::detached);
         } else {
            auto handler = handler_for(negotiated.protocol);
            if (handler) {
               co_await (*handler)(node::incoming_protocol_stream{
                   .session = relayed_session->info,
                   .protocol = negotiated.protocol,
                   .stream = std::move(negotiated.stream),
               });
            }
         }
         auto after = std::uint64_t{0};
         {
            auto lock = std::scoped_lock{mutex};
            after = metrics_value.hole_punch_successes;
         }
         if (after > before) {
            co_return hole_punch::status::succeeded;
         }
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }

   boost::asio::awaitable<void> handle_peer_exchange(fcl::quic::framed_stream framed, std::uint64_t request_id) {
      auto endpoints = std::vector<peer_exchange_message::endpoint_record>{};
      const auto snapshot = store.snapshot();
      for (const auto& record : snapshot) {
         for (const auto& endpoint : record.endpoints) {
            endpoints.push_back(peer_exchange_message::endpoint_record{
                .peer = record.peer,
                .endpoint = endpoint.endpoint,
                .capabilities = record.capabilities,
            });
            if (endpoints.size() >= options.limits.max_peer_exchange_records) {
               break;
            }
         }
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
      }
      increment_peer_exchange();
      co_await peer_exchange_codec::async_write(framed,
                                                peer_exchange_message{
                                                    .kind = peer_exchange_message::type::peer_exchange_response,
                                                    .request_id = request_id,
                                                    .peer = local,
                                                    .endpoints = std::move(endpoints),
                                                },
                                                codec_for(options));
   }

   void launch_relay_pumps(peer_id owner, fcl::p2p::stream left, fcl::p2p::stream right) {
      auto self = shared_from_this();
      struct relay_pair {
         relay_pair(peer_id owner_value, fcl::p2p::stream left_value, fcl::p2p::stream right_value)
             : owner(std::move(owner_value)), left(std::move(left_value)), right(std::move(right_value)) {}

         peer_id owner;
         fcl::p2p::stream left;
         fcl::p2p::stream right;
         std::mutex mutex;
         std::uint32_t finished = 0;
         std::uint64_t left_to_right_bytes = 0;
         std::uint64_t right_to_left_bytes = 0;
      };
      auto pair = std::make_shared<relay_pair>(std::move(owner), std::move(left), std::move(right));
      auto finish = [self, pair] {
         auto lock = std::scoped_lock{pair->mutex};
         ++pair->finished;
         if (pair->finished == 2) {
            self->finish_relay(pair->owner);
         }
      };
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto chunk = co_await pair->left.async_read();
                   if (chunk.empty()) {
                      trace_relay("pump left->right empty read");
                      break;
                   }
                   trace_relay(std::string{"pump left->right bytes="} + std::to_string(chunk.size()));
                   const auto limit = self->relay_byte_limit(pair->owner);
                   if (pair->left_to_right_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                       pair->left_to_right_bytes + chunk.size() > limit ||
                       !self->add_relay_bytes(pair->owner, chunk.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   pair->left_to_right_bytes += chunk.size();
                   co_await pair->right.async_write(chunk);
                }
             } catch (const fcl::exception::base& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                trace_relay("pump left->right failed");
                self->record_relay_failure();
             }
             try {
                co_await pair->right.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto chunk = co_await pair->right.async_read();
                   if (chunk.empty()) {
                      trace_relay("pump right->left empty read");
                      break;
                   }
                   trace_relay(std::string{"pump right->left bytes="} + std::to_string(chunk.size()));
                   const auto limit = self->relay_byte_limit(pair->owner);
                   if (pair->right_to_left_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                       pair->right_to_left_bytes + chunk.size() > limit ||
                       !self->add_relay_bytes(pair->owner, chunk.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   pair->right_to_left_bytes += chunk.size();
                   co_await pair->left.async_write(chunk);
                }
             } catch (const fcl::exception::base& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                trace_relay("pump right->left failed");
                self->record_relay_failure();
             }
             try {
                co_await pair->left.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
   }

   boost::asio::awaitable<hole_punch::status> attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                                                 std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P hole punch timeout");
      if (session_for(peer)) {
         co_return hole_punch::status::succeeded;
      }
      if (!relay_peer) {
         const auto record = store.find(peer);
         if (record) {
            for (const auto& endpoint : record->endpoints) {
               if (endpoint.relay_peer) {
                  relay_peer = endpoint.relay_peer;
                  break;
               }
            }
         }
      }
      if (!relay_peer) {
         exceptions::raise(exceptions::code::relay_not_available, "P2P hole punching requires a relay peer");
      }
      auto observed = std::vector<endpoint>{};
      if (auto local_endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*local_endpoint));
      }
      if (observed.empty()) {
         record_hole_punch_result(hole_punch::status::failed);
         co_return hole_punch::status::failed;
      }
      try {
         auto yamux = co_await open_relay_yamux(peer, *relay_peer, timeout);
         co_return co_await serve_relayed_streams_until_hole_punch(peer, relay_peer, yamux, timeout);
      } catch (...) {
         // DCUtR failures are expected on many NATs; the caller sees a typed status.
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }
};

node::node(fcl::asio::runtime& runtime, node::options options) {
   validate(options);
   impl_ = std::make_shared<impl>(runtime, std::move(options));
}

node::~node() = default;
node::node(node&&) noexcept = default;
node& node::operator=(node&&) noexcept = default;

const peer_id& node::local_peer() const noexcept {
   return impl_->local;
}

std::optional<fcl::quic::endpoint> node::local_endpoint() const {
   auto lock = std::scoped_lock{impl_->mutex};
   if (!impl_->listener) {
      return std::nullopt;
   }
   return impl_->listener->local_endpoint();
}

node::metrics_snapshot node::metrics() const {
   auto lock = std::scoped_lock{impl_->mutex};
   impl_->cleanup_expired_relay_reservations_locked();
   auto out = impl_->metrics_value;
   out.active_sessions = impl_->sessions.size();
   out.active_relay_reservations = impl_->inbound_relay_reservations.size();
   out.stopped = impl_->stopped;
   return out;
}

peer_store& node::peers() noexcept {
   return impl_->store;
}

const peer_store& node::peers() const noexcept {
   return impl_->store;
}

void node::register_protocol_handler(protocol_id protocol, node::protocol_handler handler) {
   if (protocol.value.empty() || !handler) {
      exceptions::raise(exceptions::code::invalid_options, "P2P protocol handler requires protocol id and handler");
   }
   auto lock = std::scoped_lock{impl_->mutex};
   if (impl_->handlers.size() >= impl_->options.limits.max_protocol_handlers) {
      exceptions::raise(exceptions::code::backpressure_rejected, "P2P max protocol handlers reached");
   }
   const auto [_, inserted] = impl_->handlers.emplace(std::move(protocol), std::move(handler));
   if (!inserted) {
      exceptions::raise(exceptions::code::duplicate_protocol, "duplicate P2P protocol handler");
   }
}

boost::asio::awaitable<void> node::async_listen(fcl::quic::endpoint endpoint) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         exceptions::raise(exceptions::code::closed, "P2P node is stopped");
      }
      if (self->listener) {
         exceptions::raise(exceptions::code::invalid_options, "P2P node is already listening");
      }
      self->listener =
          std::make_unique<fcl::quic::listener>(self->runtime, std::move(endpoint), self->quic_server_options());
   }
   self->launch_accept_loop();
   co_return;
}

boost::asio::awaitable<node::session_info> node::async_connect(fcl::quic::endpoint endpoint) {
   return async_connect(std::move(endpoint), connect_options{});
}

boost::asio::awaitable<node::session_info> node::async_connect(fcl::quic::endpoint endpoint,
                                                               node::connect_options options) {
   validate_operation_timeout(options.timeout, "P2P connect timeout");
   auto self = impl_;
   auto session = co_await self->connect_direct(std::move(endpoint), std::move(options));
   co_return session->info;
}

boost::asio::awaitable<void> node::async_request_peer_exchange(peer_id peer) {
   auto self = impl_;
   co_await self->request_peer_exchange(peer);
}

boost::asio::awaitable<reachability::state> node::async_probe_reachability(peer_id observer) {
   auto self = impl_;
   auto endpoints = std::vector<endpoint>{};
   if (auto endpoint = self->local_endpoint_for_control()) {
      endpoints.push_back(self->p2p_endpoint_for(*endpoint));
   }
   if (endpoints.empty()) {
      co_return reachability::state::private_network;
   }
   const auto nonce = random_nonce();
   try {
      self->remember_autonat_v2_nonce(observer, nonce);
      auto stream = co_await self->open_protocol_direct(observer, builtins::autonat_v2_dial_request,
                                                        node::open_options{}.timeout);
      co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
          .type = reachability::v2::message::kind::dial_request,
          .dial_request =
              reachability::v2::dial_request{
                  .endpoints = endpoints,
                  .nonce = nonce,
              },
      }));
      auto state = reachability::state::private_network;
      auto observed = std::optional<fcl::quic::endpoint>{};
      auto buffer = std::vector<std::uint8_t>{};
      for (auto step = 0U; step != 8U; ++step) {
         auto message = reachability::codec::decode_v2(
             co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
         if (message.type == reachability::v2::message::kind::dial_data_request && message.dial_data_request) {
            auto remaining = message.dial_data_request->bytes;
            while (remaining > 0) {
               const auto chunk_size = static_cast<std::size_t>(
                   std::min<std::uint64_t>(remaining, reachability::options{}.max_data_response_size));
               co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
                   .type = reachability::v2::message::kind::dial_data_response,
                   .dial_data_response =
                       reachability::v2::dial_data_response{
                           .data = std::vector<std::uint8_t>(chunk_size, 0x61),
                       },
               }));
               remaining -= chunk_size;
            }
            continue;
         }
         if (message.type != reachability::v2::message::kind::dial_response || !message.dial_response) {
            exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 probe expected dial response");
         }
         if (message.dial_response->status == reachability::v2::response_status::ok &&
             message.dial_response->dial_status == reachability::v2::dial_status::ok) {
            state = reachability::state::publicly_reachable;
            if (message.dial_response->index < endpoints.size()) {
               observed = endpoints[message.dial_response->index].quic_endpoint();
            }
         } else if (message.dial_response->status == reachability::v2::response_status::dial_refused ||
                    message.dial_response->dial_status == reachability::v2::dial_status::dial_back_error) {
            state = reachability::state::blocked;
         }
         self->forget_autonat_v2_nonce(observer);
         self->increment_reachability_check(state);
         self->store.mark_reachability(self->local, state, observed);
         co_return state;
      }
      exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 probe exceeded message exchange limit");
   } catch (const fcl::exception::base& error) {
      self->forget_autonat_v2_nonce(observer);
      if (p2p_code(error) != exceptions::code::unsupported_protocol) {
         throw;
      }
   }
   auto stream = co_await self->open_protocol_direct(observer, builtins::autonat_v1, node::open_options{}.timeout);
   co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
       .kind = reachability::message::message_kind::dial,
       .peer =
           reachability::peer_info{
               .peer = self->local,
               .endpoints = std::move(endpoints),
           },
   }));
   auto response = reachability::codec::decode_v1(co_await stream.async_read());
   if (response.kind != reachability::message::message_kind::dial_response || !response.response) {
      exceptions::raise(exceptions::code::protocol_error, "AutoNAT probe expected dial response");
   }
   auto state = reachability::state::private_network;
   if (response.response->status == reachability::dial_status::ok) {
      state = reachability::state::publicly_reachable;
   } else if (response.response->status == reachability::dial_status::dial_refused) {
      state = reachability::state::blocked;
   }
   self->increment_reachability_check(state);
   self->store.mark_reachability(
       self->local, state,
       response.response->endpoint ? std::make_optional(response.response->endpoint->quic_endpoint()) : std::nullopt);
   co_return state;
}

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer) {
   return async_reserve_relay(std::move(relay_peer), relay::reservation::options{});
}

boost::asio::awaitable<relay::reservation::info> node::async_reserve_relay(peer_id relay_peer,
                                                                           relay::reservation::options options) {
   auto self = impl_;
   co_return co_await self->request_relay_reservation(relay_peer, options, node::connect_options{}.timeout);
}

boost::asio::awaitable<void> node::async_cancel_relay(peer_id relay_peer) {
   auto self = impl_;
   {
      auto lock = std::scoped_lock{self->mutex};
      self->cleanup_expired_relay_reservations_locked();
      const auto it = self->outbound_relay_reservations.find(relay_peer);
      if (it == self->outbound_relay_reservations.end()) {
         co_return;
      }
      self->outbound_relay_reservations.erase(it);
   }
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer) {
   co_return co_await async_ping(std::move(peer), open_options{});
}

boost::asio::awaitable<std::chrono::milliseconds> node::async_ping(peer_id peer, open_options options) {
   auto started = std::chrono::steady_clock::now();
   auto stream = co_await async_open_protocol_stream(std::move(peer), builtins::ping, std::move(options));
   const auto payload = fcl::crypto::random_bytes(32);
   co_await stream.async_write(payload);
   const auto reply = co_await stream.async_read();
   if (reply != payload) {
      exceptions::raise(exceptions::code::protocol_error, "libp2p ping payload mismatch");
   }
   co_await stream.async_close();
   co_return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
}

boost::asio::awaitable<hole_punch::status>
node::async_attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer, std::chrono::milliseconds timeout) {
   auto self = impl_;
   co_return co_await self->attempt_hole_punch(std::move(peer), std::move(relay_peer), timeout);
}

boost::asio::awaitable<fcl::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol) {
   return async_open_protocol_stream(std::move(peer), std::move(protocol), open_options{});
}

boost::asio::awaitable<fcl::p2p::stream> node::async_open_protocol_stream(peer_id peer, protocol_id protocol,
                                                                          node::open_options options) {
   validate_operation_timeout(options.timeout, "P2P protocol open timeout");
   validate_operation_timeout(options.direct_attempt_timeout, "P2P direct attempt timeout");
   validate_operation_timeout(options.relay_attempt_timeout, "P2P relay attempt timeout");
   if (options.max_direct_endpoints == 0 || options.max_relay_candidates == 0) {
      exceptions::raise(exceptions::code::invalid_options, "P2P path attempt limits must be positive");
   }
   auto self = impl_;
   auto effective = options;
   effective.allow_relay =
       effective.allow_relay && self->options.path_policy.allow_relay && self->options.relay_policy.client_enabled;
   effective.allow_hole_punch = effective.allow_hole_punch && self->options.path_policy.allow_hole_punch;
   effective.max_direct_endpoints =
       std::min(effective.max_direct_endpoints, self->options.path_policy.max_direct_endpoints);
   effective.max_relay_candidates =
       std::min(effective.max_relay_candidates, self->options.path_policy.max_relay_candidates);
   const auto started = std::chrono::steady_clock::now();
   auto last_kind = std::optional<exceptions::code>{};
   auto last_message = std::string{};
   if (self->options.path_policy.allow_direct) {
      try {
         co_return co_await self->open_protocol_direct(
             peer, protocol, effective.timeout, effective.max_direct_endpoints, effective.direct_attempt_timeout);
      } catch (const fcl::exception::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
         if (p2p_code(error) == exceptions::code::unsupported_protocol || p2p_code(error) == exceptions::code::protocol_error ||
             p2p_code(error) == exceptions::code::codec_error) {
            throw;
         }
         try {
            (void)remaining_timeout(started, effective.timeout, "P2P protocol open");
         } catch (const fcl::exception::base&) {
            throw;
         }
         if (!effective.allow_relay && !(effective.allow_hole_punch && effective.relay_peer)) {
            throw;
         }
      }
   }

   auto relay_candidates = std::vector<peer_id>{};
   if (effective.relay_peer) {
      relay_candidates.push_back(*effective.relay_peer);
   } else if (effective.allow_relay || effective.allow_hole_punch) {
      const auto snapshot = self->store.snapshot();
      auto relay_records = std::vector<peer_store::record>{};
      for (const auto& record : snapshot) {
         if (record.peer == peer) {
            continue;
         }
         if (!record.capabilities.has(capabilities::relay) ||
             !record.capabilities.has(capabilities::relay_reservation)) {
            continue;
         }
         relay_records.push_back(record);
      }
      std::stable_sort(relay_records.begin(), relay_records.end(),
                       [](const auto& left, const auto& right) { return left.score > right.score; });
      for (const auto& record : relay_records) {
         if (relay_candidates.size() >= effective.max_relay_candidates) {
            break;
         }
         relay_candidates.push_back(record.peer);
      }
   }

   if (effective.allow_hole_punch) {
      for (const auto& relay_peer : relay_candidates) {
         const auto remaining = remaining_timeout(started, effective.timeout, "P2P hole punch");
         const auto per_attempt = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P hole punch attempt");
         try {
            const auto status = co_await self->attempt_hole_punch(peer, relay_peer, per_attempt);
            if (status == hole_punch::status::succeeded) {
               co_return co_await self->open_protocol_direct(
                   peer, protocol, remaining_timeout(started, effective.timeout, "P2P protocol open after hole punch"),
                   effective.max_direct_endpoints, effective.direct_attempt_timeout);
            }
         } catch (const fcl::exception::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
         }
      }
   }

   if (!effective.allow_relay) {
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::relay_not_available, "P2P relay fallback is disabled");
   }

   if (relay_candidates.empty()) {
      exceptions::raise(exceptions::code::relay_not_available, "P2P path manager found no reserved relay candidate");
   }
   self->record_direct_failure(peer);
   for (const auto& relay_peer : relay_candidates) {
      const auto remaining = remaining_timeout(started, effective.timeout, "P2P protocol open");
      const auto per_attempt = attempt_timeout(remaining, effective.relay_attempt_timeout, "P2P relay path attempt");
      try {
         co_return co_await self->open_protocol_via_relay(peer, protocol, relay_peer, per_attempt);
      } catch (const fcl::exception::base& error) {
         last_kind = p2p_code(error);
         last_message = error.what();
      }
   }
   if (last_kind) {
      exceptions::raise(*last_kind, last_message);
   }
   exceptions::raise(exceptions::code::relay_not_available, "P2P path manager exhausted relay candidates");
}

boost::asio::awaitable<void> node::async_stop() {
   auto self = impl_;
   std::vector<std::shared_ptr<impl::session_state>> sessions;
   {
      auto lock = std::scoped_lock{self->mutex};
      if (self->stopped) {
         co_return;
      }
      self->stopped = true;
      if (self->listener) {
         self->listener->stop();
      }
      for (auto& [_, session] : self->sessions) {
         session->closed = true;
         sessions.push_back(session);
      }
      self->sessions.clear();
      self->inbound_relay_reservations.clear();
      self->outbound_relay_reservations.clear();
      self->metrics_value.active_sessions = 0;
      self->metrics_value.active_relay_reservations = 0;
      self->metrics_value.stopped = true;
   }
   for (auto& session : sessions) {
      try {
         co_await session->connection.async_close();
      } catch (...) {
         session->connection.cancel();
      }
   }
}

void node::stop() {
   {
      auto lock = std::scoped_lock{impl_->mutex};
      if (impl_->stopped) {
         return;
      }
      impl_->stopped = true;
      if (impl_->listener) {
         impl_->listener->stop();
      }
      for (auto& [_, session] : impl_->sessions) {
         session->closed = true;
         session->connection.cancel();
      }
      impl_->sessions.clear();
      impl_->inbound_relay_reservations.clear();
      impl_->outbound_relay_reservations.clear();
      impl_->metrics_value.active_sessions = 0;
      impl_->metrics_value.active_relay_reservations = 0;
      impl_->metrics_value.stopped = true;
   }
}

} // namespace fcl::p2p
