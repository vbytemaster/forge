module;

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
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
import fcl.crypto.asymmetric;
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

#include "peer_exchange_codec.hpp"

namespace fcl::p2p {

namespace {
inline constexpr std::uint16_t peer_exchange_wire_version_v1 = 1;
inline constexpr std::uint32_t mandatory_flag_mask = 0x8000'0000U;
} // namespace

namespace peer_exchange_codec {

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

[[nodiscard]] std::vector<std::uint8_t> encode(const peer_exchange_message& message, options opts) {
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

[[nodiscard]] peer_exchange_message decode(std::span<const std::uint8_t> bytes, options opts) {
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
                                         options opts) {
   auto encoded = encode(message, opts);
   co_await stream.async_write_frame(encoded);
}

boost::asio::awaitable<peer_exchange_message> async_read(fcl::quic::framed_stream& stream, options opts) {
   auto encoded = co_await stream.async_read_frame();
   co_return decode(encoded, opts);
}

} // namespace peer_exchange_codec

} // namespace fcl::p2p
