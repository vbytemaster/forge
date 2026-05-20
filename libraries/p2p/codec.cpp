module;

#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.codec;

import fcl.p2p.errors;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.quic.endpoint;

namespace fcl::p2p {
namespace {

inline constexpr std::uint8_t magic[] = {'S', 'L', 'P', '2'};

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
      throw_p2p_error(error_kind::codec_error, "P2P string field is too large");
   }
   append_u32(out, static_cast<std::uint32_t>(value.size()));
   out.insert(out.end(), value.begin(), value.end());
}

void append_bytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& value) {
   if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw_p2p_error(error_kind::codec_error, "P2P bytes field is too large");
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
         throw_p2p_error(error_kind::codec_error, "invalid P2P message magic");
      }
      offset_ += sizeof(magic);
   }

   void expect_end() const {
      if (offset_ != bytes_.size()) {
         throw_p2p_error(error_kind::codec_error, "trailing bytes in P2P message");
      }
   }

 private:
   void require(std::size_t size) const {
      if (size > bytes_.size() - offset_) {
         throw_p2p_error(error_kind::codec_error, "truncated P2P message");
      }
   }

   std::span<const std::uint8_t> bytes_;
   std::size_t offset_ = 0;
};

[[nodiscard]] control_message::type checked_control_kind(std::uint16_t value) {
   switch (static_cast<control_message::type>(value)) {
   case control_message::type::hello:
   case control_message::type::hello_ack:
   case control_message::type::protocol_open:
   case control_message::type::protocol_accept:
   case control_message::type::protocol_reject:
   case control_message::type::peer_exchange_request:
   case control_message::type::peer_exchange_response:
   case control_message::type::relay_open:
   case control_message::type::relay_accept:
   case control_message::type::relay_reject:
   case control_message::type::relay_close:
   case control_message::type::ping:
   case control_message::type::pong:
   case control_message::type::goaway:
   case control_message::type::reachability_probe:
   case control_message::type::reachability_result:
   case control_message::type::relay_reserve:
   case control_message::type::relay_renew:
   case control_message::type::relay_cancel:
   case control_message::type::relay_reserved:
   case control_message::type::hole_punch_prepare:
   case control_message::type::hole_punch_sync:
   case control_message::type::hole_punch_result:
      return static_cast<control_message::type>(value);
   }
   throw_p2p_error(error_kind::codec_error, "unknown P2P message type");
}

[[nodiscard]] reachability_state checked_reachability_state(std::uint16_t value) {
   switch (static_cast<reachability_state>(value)) {
   case reachability_state::unknown:
   case reachability_state::publicly_reachable:
   case reachability_state::private_network:
   case reachability_state::blocked:
   case reachability_state::relay_only:
      return static_cast<reachability_state>(value);
   }
   throw_p2p_error(error_kind::codec_error, "unknown P2P reachability state");
}

[[nodiscard]] hole_punch_status checked_hole_punch_status(std::uint16_t value) {
   switch (static_cast<hole_punch_status>(value)) {
   case hole_punch_status::not_attempted:
   case hole_punch_status::prepared:
   case hole_punch_status::synced:
   case hole_punch_status::succeeded:
   case hole_punch_status::failed:
      return static_cast<hole_punch_status>(value);
   }
   throw_p2p_error(error_kind::codec_error, "unknown P2P hole punch status");
}

} // namespace

namespace control_codec {

std::vector<std::uint8_t> encode(const control_message& message, options opts) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(128 + message.payload.size());
   out.insert(out.end(), std::begin(magic), std::end(magic));
   append_u16(out, wire_version_v1);
   append_u16(out, static_cast<std::uint16_t>(message.kind));
   append_u32(out, message.flags);
   append_u64(out, message.request_id);
   append_string(out, message.peer.value);
   append_string(out, message.protocol.value);
   append_u64(out, message.capabilities.bits);
   append_u64(out, message.max_frame_size);
   append_u64(out, message.reservation_id);
   append_u64(out, message.ttl_ms);
   append_u64(out, message.max_streams);
   append_u64(out, message.max_bytes);
   append_u64(out, message.max_queued_bytes);
   append_u16(out, static_cast<std::uint16_t>(message.reachability));
   append_u16(out, static_cast<std::uint16_t>(message.hole_punch));
   append_string(out, message.reason);
   if (message.endpoints.size() > opts.max_endpoint_records) {
      throw_p2p_error(error_kind::codec_error, "too many P2P endpoint records");
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
      throw_p2p_error(error_kind::codec_error, "P2P message exceeds max size");
   }
   return out;
}

control_message decode(std::span<const std::uint8_t> bytes, options opts) {
   if (bytes.size() > opts.max_message_size) {
      throw_p2p_error(error_kind::codec_error, "P2P message exceeds max size");
   }
   auto in = reader{bytes};
   in.require_magic();
   if (in.u16() != wire_version_v1) {
      throw_p2p_error(error_kind::codec_error, "unsupported P2P wire version");
   }
   auto out = control_message{};
   out.kind = checked_control_kind(in.u16());
   out.flags = in.u32();
   if ((out.flags & mandatory_flag_mask) != 0) {
      throw_p2p_error(error_kind::codec_error, "unknown mandatory P2P message flags");
   }
   out.request_id = in.u64();
   out.peer = peer_id{.value = in.string()};
   out.protocol = protocol_id{.value = in.string()};
   out.capabilities = capability_set{.bits = in.u64()};
   out.max_frame_size = in.u64();
   out.reservation_id = in.u64();
   out.ttl_ms = in.u64();
   out.max_streams = in.u64();
   out.max_bytes = in.u64();
   out.max_queued_bytes = in.u64();
   out.reachability = checked_reachability_state(in.u16());
   out.hole_punch = checked_hole_punch_status(in.u16());
   out.reason = in.string();
   const auto endpoint_count = in.u32();
   if (endpoint_count > opts.max_endpoint_records) {
      throw_p2p_error(error_kind::codec_error, "too many P2P endpoint records");
   }
   out.endpoints.reserve(endpoint_count);
   for (auto i = std::uint32_t{0}; i != endpoint_count; ++i) {
      out.endpoints.push_back(control_message::endpoint_record{
          .peer = peer_id{.value = in.string()},
          .endpoint = fcl::quic::endpoint{.host = in.string(), .port = in.u16()},
          .capabilities = capability_set{.bits = in.u64()},
      });
   }
   out.payload = in.bytes();
   in.expect_end();
   return out;
}

boost::asio::awaitable<void> async_write(fcl::quic::framed_stream& stream, const control_message& message,
                                         options opts) {
   auto encoded = encode(message, opts);
   co_await stream.async_write_frame(encoded);
}

boost::asio::awaitable<control_message> async_read(fcl::quic::framed_stream& stream, options opts) {
   auto encoded = co_await stream.async_read_frame();
   co_return decode(encoded, opts);
}

} // namespace control_codec

} // namespace fcl::p2p
