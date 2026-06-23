module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

module forge.p2p.node;

import forge.crypto.chacha20_poly1305;
import forge.crypto.hmac;
import forge.crypto.pem;
import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.crypto.x25519;
import forge.p2p.exceptions;
import forge.p2p.identity;
import forge.p2p.message;
import forge.p2p.negotiation;
import forge.p2p.stream;
import forge.multiformats.exceptions;
import forge.multiformats.varint;
import forge.stcp.connection;
import forge.stcp.exceptions;
import forge.transport.stream;
import forge.tcp.connection;
import forge.yamux.session;

#include "identity_signature.hpp"
#include "libp2p_tls.hpp"
#include "protobuf.hpp"
#include "stream_upgrade.hpp"
#include "tcp_stream_upgrade.hpp"

namespace forge::p2p {
namespace {

[[noreturn]] void throw_crypto_failure(std::string message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_identity, std::move(message));
}

[[nodiscard]] std::vector<std::uint8_t> sha256(std::span<const std::uint8_t> value) {
   const auto digest = forge::crypto::sha256::hash(value).to_uint8_span();
   return {digest.begin(), digest.end()};
}

[[nodiscard]] std::vector<std::uint8_t> hmac_sha256(std::span<const std::uint8_t> key,
                                                    std::span<const std::uint8_t> value) {
   const auto digest = forge::crypto::hmac_sha256{}.digest(key, value).to_uint8_span();
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

[[nodiscard]] forge::crypto::asymmetric::private_key private_key_from_pem_for_noise(std::string_view pem) {
   try {
      return forge::crypto::pem::read_private_key(pem);
   } catch (const forge::exceptions::base& error) {
      throw_crypto_failure(error.what());
   }
}

struct x25519_key {
   forge::crypto::x25519::private_key key;
   std::array<std::uint8_t, 32> public_key{};
};

[[nodiscard]] x25519_key make_x25519_key() {
   auto key = forge::crypto::x25519::private_key::generate();
   return x25519_key{.key = key, .public_key = key.get_public_key().serialize()};
}

[[nodiscard]] std::vector<std::uint8_t> x25519_dh(const forge::crypto::x25519::private_key& private_key,
                                                  std::span<const std::uint8_t, 32> remote_public) {
   auto public_key = forge::crypto::x25519::public_key_data{};
   std::copy(remote_public.begin(), remote_public.end(), public_key.begin());
   const auto secret = private_key.get_shared_secret(forge::crypto::x25519::public_key{public_key});
   return {secret.begin(), secret.end()};
}

[[nodiscard]] std::array<std::uint8_t, 32> checked_x25519_public(std::span<const std::uint8_t> bytes) {
   if (bytes.size() != 32) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise X25519 public key must be 32 bytes");
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
   if (key.size() != forge::crypto::chacha20_poly1305::key{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise cipher key must be 32 bytes");
   }
   auto cipher_key = forge::crypto::chacha20_poly1305::key{};
   std::copy(key.begin(), key.end(), cipher_key.begin());
   const auto nonce = noise_nonce(nonce_value);
   return forge::crypto::chacha20_poly1305::encrypt(cipher_key, nonce, ad, plaintext);
}

[[nodiscard]] std::vector<std::uint8_t> chacha20_poly1305_decrypt(std::span<const std::uint8_t> key,
                                                                  std::uint64_t nonce_value,
                                                                  std::span<const std::uint8_t> ad,
                                                                  std::span<const std::uint8_t> ciphertext) {
   if (ciphertext.size() < 16) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise ciphertext is missing authentication tag");
   }
   if (key.size() != forge::crypto::chacha20_poly1305::key{}.size()) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise cipher key must be 32 bytes");
   }
   auto cipher_key = forge::crypto::chacha20_poly1305::key{};
   std::copy(key.begin(), key.end(), cipher_key.begin());
   const auto nonce = noise_nonce(nonce_value);
   try {
      return forge::crypto::chacha20_poly1305::decrypt(cipher_key, nonce, ad, ciphertext);
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "Noise authentication failed");
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
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "Noise handshake requires libp2p identity key material");
   }
   auto private_key = private_key_from_pem_for_noise(options.private_key_pem);
   auto identity_key = options.public_key;
   if (identity_key.empty()) {
      identity_key = encode_public_key(public_key_from_crypto(private_key.get_public_key()));
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
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "Noise handshake payload is missing identity proof");
   }
   const auto key = decode_public_key(payload.identity_key);
   const auto peer = make_peer_id(key);
   if (expected_peer && peer != *expected_peer) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "Noise identity peer id mismatch");
   }
   if (!verify_identity_signature(key, noise_signature_payload(static_key), payload.identity_signature)) {
      FORGE_THROW_EXCEPTION(exceptions::peer_verification_failed, "Noise identity signature is invalid");
   }
   return verified_noise_payload{
       .peer = peer,
       .supports_yamux = std::ranges::contains(payload.stream_muxers, std::string{"/yamux/1.0.0"}),
   };
}

class secure_io : public std::enable_shared_from_this<secure_io> {
 public:
   explicit secure_io(forge::p2p::stream stream) : stream_(std::move(stream)) {}

   [[nodiscard]] bool valid() const noexcept {
      return stream_.valid();
   }

   [[nodiscard]] std::int64_t id() const noexcept {
      return stream_.id();
   }

   boost::asio::awaitable<void> write_plain_frame(std::span<const std::uint8_t> bytes) {
      if (bytes.size() > std::numeric_limits<std::uint16_t>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "Noise frame is too large");
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

   void cancel() {
      stream_.cancel();
   }

 private:
   boost::asio::awaitable<std::vector<std::uint8_t>> read_exact(std::size_t size) {
      while (buffer_.size() < size) {
         auto chunk = co_await stream_.async_read();
         if (chunk.empty()) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "Noise stream closed");
         }
         buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
      }
      auto out = std::vector<std::uint8_t>{buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size)};
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size));
      co_return out;
   }

   forge::p2p::stream stream_;
   std::vector<std::uint8_t> buffer_;
   noise_cipher_state read_state_;
   noise_cipher_state write_state_;
};

class secure_stream_concept final : public forge::transport::detail::stream_concept {
 public:
   explicit secure_stream_concept(std::shared_ptr<secure_io> secure) : secure_(std::move(secure)) {}

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

   void cancel() override {
      if (secure_) {
         secure_->cancel();
      }
   }

 private:
   std::shared_ptr<secure_io> secure_;
};

[[nodiscard]] forge::transport::stream secure_transport_stream(std::shared_ptr<secure_io> secure) {
   return forge::transport::detail::stream_access::make(
       std::make_shared<secure_stream_concept>(std::move(secure)));
}

struct noise_result {
   peer_id peer;
   std::shared_ptr<secure_io> secure;
   bool early_yamux = false;
};

boost::asio::awaitable<noise_result> noise_initiator(forge::p2p::stream stream, const node::options& options,
                                                     std::optional<peer_id> expected_peer) {
   auto io = std::make_shared<secure_io>(std::move(stream));
   auto symmetric = noise_symmetric_state{};
   auto ephemeral = make_x25519_key();
   auto local_static = make_x25519_key();

   symmetric.mix_hash(ephemeral.public_key);
   (void)symmetric.encrypt_and_hash(std::span<const std::uint8_t>{});
   co_await io->write_plain_frame(ephemeral.public_key);

   auto message2 = co_await io->read_plain_frame();
   if (message2.size() < 48) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise responder message is truncated");
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
   co_return noise_result{.peer = verified_responder.peer, .secure = std::move(io),
                          .early_yamux = verified_responder.supports_yamux};
}

boost::asio::awaitable<noise_result> noise_responder(forge::p2p::stream stream, const node::options& options,
                                                     std::optional<peer_id> expected_peer) {
   auto io = std::make_shared<secure_io>(std::move(stream));
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
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "Noise initiator message is truncated");
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
   co_return noise_result{.peer = verified_initiator.peer, .secure = std::move(io),
                          .early_yamux = verified_initiator.supports_yamux};
}

template <typename Connection>
class exact_negotiation_io {
 public:
   explicit exact_negotiation_io(Connection& connection) : connection_(connection) {}

   boost::asio::awaitable<void> write(protocol_negotiation::message value) {
      const auto payload = protocol_negotiation::encode_message(value);
      const auto frame = protocol_negotiation::encode_frame(payload);
      co_await connection_.async_write(frame);
   }

   boost::asio::awaitable<protocol_negotiation::message> read() {
      while (true) {
         try {
            auto frame = protocol_negotiation::decode_frame(buffer_);
            auto payload = std::move(frame.payload);
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame.consumed));
            co_return protocol_negotiation::decode_message(payload);
         } catch (const forge::exceptions::base& error) {
            const auto code = exceptions::code_of(error);
            if (!code || *code != exceptions::code::closed) {
               throw;
            }
         }
         auto byte = std::array<std::uint8_t, 1>{};
         const auto size = co_await connection_.async_read_some(byte);
         if (size == 0) {
            FORGE_THROW_EXCEPTION(exceptions::closed, "multistream-select connection closed");
         }
         buffer_.push_back(byte.front());
      }
   }

 private:
   Connection& connection_;
   std::vector<std::uint8_t> buffer_;
};

template <typename Connection>
boost::asio::awaitable<protocol_id> select_protocol(Connection& connection, std::span<const protocol_id> protocols) {
   auto io = exact_negotiation_io<Connection>{connection};
   co_await io.write(protocol_negotiation::message{.kind = protocol_negotiation::message_kind::header,
                                                   .protocol = protocol_negotiation::multistream_v1});
   auto first = true;
   for (const auto& protocol : protocols) {
      co_await io.write(protocol_negotiation::message{.kind = protocol_negotiation::message_kind::protocol,
                                                      .protocol = protocol});
      if (first) {
         auto header = co_await io.read();
         if (header.kind != protocol_negotiation::message_kind::header) {
            FORGE_THROW_EXCEPTION(exceptions::protocol_error, "multistream-select expected security header response");
         }
         first = false;
      }
      auto selected = co_await io.read();
      if (selected.kind == protocol_negotiation::message_kind::not_available) {
         continue;
      }
      if (selected.kind != protocol_negotiation::message_kind::protocol || selected.protocol.value != protocol.value) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "multistream-select selected unexpected security protocol");
      }
      co_return protocol;
   }
   FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "remote peer supports no compatible security protocol");
}

template <typename Connection>
boost::asio::awaitable<protocol_id> accept_protocol(Connection& connection, std::span<const protocol_id> protocols) {
   auto io = exact_negotiation_io<Connection>{connection};
   auto header = co_await io.read();
   if (header.kind != protocol_negotiation::message_kind::header) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error, "multistream-select expected security header");
   }
   co_await io.write(protocol_negotiation::message{.kind = protocol_negotiation::message_kind::header,
                                                   .protocol = protocol_negotiation::multistream_v1});
   while (true) {
      auto proposal = co_await io.read();
      if (proposal.kind != protocol_negotiation::message_kind::protocol) {
         FORGE_THROW_EXCEPTION(exceptions::protocol_error, "multistream-select expected security protocol proposal");
      }
      const auto found = std::ranges::find_if(protocols, [&proposal](const auto& value) {
         return value.value == proposal.protocol.value;
      });
      if (found != protocols.end()) {
         co_await io.write(protocol_negotiation::message{.kind = protocol_negotiation::message_kind::protocol,
                                                         .protocol = *found});
         co_return *found;
      }
      co_await io.write(protocol_negotiation::message{.kind = protocol_negotiation::message_kind::not_available,
                                                      .protocol = protocol_negotiation::not_available});
   }
}

template <typename Connection>
boost::asio::awaitable<void> negotiate_yamux(Connection& connection, bool outbound) {
   const auto yamux = protocol_id{.value = "/yamux/1.0.0"};
   if (outbound) {
      auto selected = co_await select_protocol(connection, std::span<const protocol_id>{&yamux, 1});
      (void)selected;
      co_return;
   }
   auto selected = co_await accept_protocol(connection, std::span<const protocol_id>{&yamux, 1});
   (void)selected;
}

[[nodiscard]] exceptions::code map_stcp_error(forge::stcp::exceptions::code kind) noexcept {
   using stcp_kind = forge::stcp::exceptions::code;
   switch (kind) {
   case stcp_kind::invalid_endpoint:
   case stcp_kind::invalid_options:
      return exceptions::code::invalid_options;
   case stcp_kind::connect_failed:
   case stcp_kind::listen_failed:
   case stcp_kind::accept_failed:
   case stcp_kind::io_error:
      return exceptions::code::internal;
   case stcp_kind::verification_failed:
   case stcp_kind::handshake_failed:
      return exceptions::code::peer_verification_failed;
   case stcp_kind::canceled:
      return exceptions::code::canceled;
   case stcp_kind::closed:
      return exceptions::code::closed;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_stcp_as_p2p(const forge::exceptions::base& error) {
   const auto code = forge::stcp::exceptions::code_of(error);
   if (code) {
      FORGE_THROW_CODE(map_stcp_error(*code), error.what());
   }
   throw;
}

void set_cancel(tcp_upgrade_deadline& deadline, std::function<void()> cancel) {
   if (deadline.cancel_current) {
      *deadline.cancel_current = std::move(cancel);
   }
}

void clear_cancel(tcp_upgrade_deadline& deadline) noexcept {
   if (!deadline.cancel_current) {
      return;
   }
   try {
      *deadline.cancel_current = {};
   } catch (...) {
   }
}

struct cancel_cleanup {
   tcp_upgrade_deadline* deadline = nullptr;

   ~cancel_cleanup() {
      if (deadline) {
         clear_cancel(*deadline);
      }
   }
};

[[nodiscard]] bool has_timeout(const tcp_upgrade_deadline& deadline) noexcept {
   return deadline.timeout.count() > 0;
}

boost::asio::awaitable<upgraded_session> finish_noise_outbound(forge::p2p::stream stream, const node::options& options,
                                                               std::optional<peer_id> expected_peer,
                                                               tcp_upgrade_deadline deadline = {}) {
   auto cleanup = cancel_cleanup{&deadline};
   set_cancel(deadline, [&stream] { stream.cancel(); });
   auto secure = co_await noise_initiator(std::move(stream), options,
                                          options.allow_insecure_test_mode ? std::nullopt : std::move(expected_peer));
   set_cancel(deadline, [secure = secure.secure] { secure->cancel(); });
   if (!secure.early_yamux) {
      (void)co_await protocol_negotiation::async_select(secure_transport_stream(secure.secure),
                                                        protocol_id{.value = "/yamux/1.0.0"});
   }
   auto yamux = std::make_shared<forge::yamux::session>(secure_transport_stream(std::move(secure.secure)),
                                                      forge::yamux::side::initiator);
   co_return upgraded_session{.peer = std::move(secure.peer), .session = std::move(yamux)};
}

boost::asio::awaitable<upgraded_session> finish_noise_inbound(forge::p2p::stream stream, const node::options& options,
                                                              std::optional<peer_id> expected_peer,
                                                              tcp_upgrade_deadline deadline = {}) {
   auto cleanup = cancel_cleanup{&deadline};
   set_cancel(deadline, [&stream] { stream.cancel(); });
   auto secure =
       co_await noise_responder(std::move(stream), options,
                                options.allow_insecure_test_mode ? std::nullopt : std::move(expected_peer));
   set_cancel(deadline, [secure = secure.secure] { secure->cancel(); });
   if (!secure.early_yamux) {
      (void)co_await protocol_negotiation::async_accept(secure_transport_stream(secure.secure),
                                                        {protocol_id{.value = "/yamux/1.0.0"}});
   }
   auto yamux = std::make_shared<forge::yamux::session>(secure_transport_stream(std::move(secure.secure)),
                                                      forge::yamux::side::responder);
   co_return upgraded_session{.peer = std::move(secure.peer), .session = std::move(yamux)};
}

boost::asio::awaitable<upgraded_session> finish_tls_outbound(forge::tcp::connection connection,
                                                             const node::options& options,
                                                             std::optional<peer_id> expected_peer,
                                                             tcp_upgrade_deadline deadline = {}) {
   auto cleanup = cancel_cleanup{&deadline};
   try {
      set_cancel(deadline, [&connection] { connection.cancel(); });
      auto tls = has_timeout(deadline)
                     ? co_await forge::stcp::async_upgrade_client(std::move(connection),
                                                                make_libp2p_tls_client_options(options),
                                                                deadline.timeout)
                     : co_await forge::stcp::async_upgrade_client(std::move(connection),
                                                                make_libp2p_tls_client_options(options));
      set_cancel(deadline, [&tls] { tls.cancel(); });
      const auto peer = verify_libp2p_tls_chain(tls.peer_certificate_chain(),
                                                options.allow_insecure_test_mode ? std::nullopt : expected_peer);
      const auto selected_alpn = tls.selected_alpn();
      if (selected_alpn.empty() || selected_alpn == "libp2p") {
         co_await negotiate_yamux(tls, true);
      } else if (selected_alpn != "/yamux/1.0.0") {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "libp2p TLS selected unsupported muxer");
      }
      auto stream = std::move(tls).into_transport_stream();
      auto yamux = std::make_shared<forge::yamux::session>(std::move(stream.stream), forge::yamux::side::initiator);
      co_return upgraded_session{.peer = peer, .session = std::move(yamux)};
   } catch (const forge::exceptions::base& error) {
      rethrow_stcp_as_p2p(error);
   }
}

boost::asio::awaitable<upgraded_session> finish_tls_inbound(forge::tcp::connection connection,
                                                            const node::options& options,
                                                            std::optional<peer_id> expected_peer,
                                                            tcp_upgrade_deadline deadline = {}) {
   auto cleanup = cancel_cleanup{&deadline};
   try {
      set_cancel(deadline, [&connection] { connection.cancel(); });
      auto tls = has_timeout(deadline)
                     ? co_await forge::stcp::async_upgrade_server(std::move(connection),
                                                                make_libp2p_tls_server_options(options),
                                                                deadline.timeout)
                     : co_await forge::stcp::async_upgrade_server(std::move(connection),
                                                                make_libp2p_tls_server_options(options));
      set_cancel(deadline, [&tls] { tls.cancel(); });
      const auto peer = verify_libp2p_tls_chain(tls.peer_certificate_chain(),
                                                options.allow_insecure_test_mode ? std::nullopt : expected_peer);
      const auto selected_alpn = tls.selected_alpn();
      if (selected_alpn.empty() || selected_alpn == "libp2p") {
         co_await negotiate_yamux(tls, false);
      } else if (selected_alpn != "/yamux/1.0.0") {
         FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "libp2p TLS selected unsupported muxer");
      }
      auto stream = std::move(tls).into_transport_stream();
      auto yamux = std::make_shared<forge::yamux::session>(std::move(stream.stream), forge::yamux::side::responder);
      co_return upgraded_session{.peer = peer, .session = std::move(yamux)};
   } catch (const forge::exceptions::base& error) {
      rethrow_stcp_as_p2p(error);
   }
}

} // namespace

boost::asio::awaitable<upgraded_session>
upgrade_outbound_stream(forge::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer) {
   const auto noise_protocol = protocol_id{.value = "/noise"};
   auto noise_stream = co_await protocol_negotiation::async_select(std::move(stream), noise_protocol);
   co_return co_await finish_noise_outbound(std::move(noise_stream), options, std::move(expected_peer));
}

boost::asio::awaitable<upgraded_session>
upgrade_inbound_stream(forge::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer) {
   const auto noise_protocol = protocol_id{.value = "/noise"};
   auto noise_stream = co_await protocol_negotiation::async_accept(std::move(stream), {noise_protocol});
   co_return co_await finish_noise_inbound(std::move(noise_stream.stream), options, std::move(expected_peer));
}

boost::asio::awaitable<upgraded_session>
upgrade_outbound_tcp(forge::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer) {
   co_return co_await upgrade_outbound_tcp(std::move(connection), options, std::move(expected_peer), {});
}

boost::asio::awaitable<upgraded_session> upgrade_outbound_tcp(forge::tcp::connection connection,
                                                              const node::options& options,
                                                              std::optional<peer_id> expected_peer,
                                                              tcp_upgrade_deadline deadline) {
   auto cleanup = cancel_cleanup{&deadline};
   set_cancel(deadline, [&connection] { connection.cancel(); });
   const auto protocols = std::array{
       protocol_id{.value = "/tls/1.0.0"},
       protocol_id{.value = "/noise"},
   };
   const auto selected = co_await select_protocol(connection, protocols);
   if (selected.value == "/tls/1.0.0") {
      co_return co_await finish_tls_outbound(std::move(connection), options, std::move(expected_peer), deadline);
   }
   auto stream = std::move(connection).into_transport_stream();
   co_return co_await finish_noise_outbound(forge::p2p::stream{std::move(stream.stream)}, options,
                                            std::move(expected_peer), deadline);
}

boost::asio::awaitable<upgraded_session>
upgrade_inbound_tcp(forge::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer) {
   co_return co_await upgrade_inbound_tcp(std::move(connection), options, std::move(expected_peer), {});
}

boost::asio::awaitable<upgraded_session> upgrade_inbound_tcp(forge::tcp::connection connection,
                                                             const node::options& options,
                                                             std::optional<peer_id> expected_peer,
                                                             tcp_upgrade_deadline deadline) {
   auto cleanup = cancel_cleanup{&deadline};
   set_cancel(deadline, [&connection] { connection.cancel(); });
   const auto protocols = std::array{
       protocol_id{.value = "/tls/1.0.0"},
       protocol_id{.value = "/noise"},
   };
   const auto selected = co_await accept_protocol(connection, protocols);
   if (selected.value == "/tls/1.0.0") {
      co_return co_await finish_tls_inbound(std::move(connection), options, std::move(expected_peer), deadline);
   }
   auto stream = std::move(connection).into_transport_stream();
   co_return co_await finish_noise_inbound(forge::p2p::stream{std::move(stream.stream)}, options,
                                           std::move(expected_peer), deadline);
}

} // namespace forge::p2p
