#pragma once

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

} // namespace fcl::p2p
