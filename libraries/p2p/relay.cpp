module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.p2p.relay;

import fcl.multiformats.address;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.exceptions;
import fcl.p2p.exceptions;

#include "protobuf.hpp"

namespace fcl::p2p {
namespace {

[[nodiscard]] std::vector<std::uint8_t> endpoint_bytes(const endpoint& value) {
   return fcl::multiformats::address::parse(value.to_string()).to_bytes();
}

[[nodiscard]] endpoint endpoint_from_bytes(std::span<const std::uint8_t> value) {
   return parse_endpoint(fcl::multiformats::address::from_bytes(value).to_string());
}

void append_peer(std::vector<std::uint8_t>& out, std::uint32_t field, const relay::peer& value) {
   auto encoded = std::vector<std::uint8_t>{};
   detail::append_bytes(encoded, 1, value.id.to_bytes());
   for (const auto& endpoint : value.endpoints) {
      const auto bytes = endpoint_bytes(endpoint);
      detail::append_bytes(encoded, 2, bytes);
   }
   detail::append_bytes(out, field, encoded);
}

[[nodiscard]] relay::peer decode_peer(std::span<const std::uint8_t> bytes) {
   auto out = relay::peer{};
   auto in = detail::reader{bytes};
   auto saw_id = false;
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (type != detail::wire_type::length_delimited) {
         in.skip(type);
         continue;
      }
      switch (field) {
      case 1:
         out.id = peer_id::from_bytes(in.bytes());
         saw_id = true;
         break;
      case 2:
         out.endpoints.push_back(endpoint_from_bytes(in.bytes()));
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_id) {
      exceptions::raise(exceptions::code::codec_error, "relay peer is missing id");
   }
   return out;
}

void append_reservation(std::vector<std::uint8_t>& out, std::uint32_t field, const relay::reservation& value) {
   auto encoded = std::vector<std::uint8_t>{};
   detail::append_uint64(encoded, 1, value.expires_at);
   for (const auto& endpoint : value.relay_endpoints) {
      const auto bytes = endpoint_bytes(endpoint);
      detail::append_bytes(encoded, 2, bytes);
   }
   if (value.voucher) {
      const auto bytes = value.voucher->encode();
      detail::append_bytes(encoded, 3, bytes);
   }
   detail::append_bytes(out, field, encoded);
}

[[nodiscard]] relay::reservation decode_reservation(std::span<const std::uint8_t> bytes) {
   auto out = relay::reservation{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay reservation expire must be varint");
         }
         out.expires_at = in.read_varint();
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay reservation address must be bytes");
         }
         out.relay_endpoints.push_back(endpoint_from_bytes(in.bytes()));
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay reservation voucher must be bytes");
         }
         out.voucher = signed_envelope::decode(in.bytes());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

void append_limit(std::vector<std::uint8_t>& out, std::uint32_t field, const relay::limit& value) {
   auto encoded = std::vector<std::uint8_t>{};
   detail::append_uint64(encoded, 1, static_cast<std::uint64_t>(value.duration.count()));
   detail::append_uint64(encoded, 2, value.data);
   detail::append_bytes(out, field, encoded);
}

[[nodiscard]] relay::limit decode_limit(std::span<const std::uint8_t> bytes) {
   auto out = relay::limit{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (type != detail::wire_type::varint) {
         in.skip(type);
         continue;
      }
      if (field == 1) {
         out.duration = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
      } else if (field == 2) {
         out.data = in.read_varint();
      } else {
         in.skip(type);
      }
   }
   return out;
}

[[nodiscard]] relay::hop_message::message_kind checked_hop_kind(std::uint64_t value) {
   if (value == static_cast<std::uint16_t>(relay::hop_message::message_kind::reserve)) {
      return relay::hop_message::message_kind::reserve;
   }
   if (value == static_cast<std::uint16_t>(relay::hop_message::message_kind::connect)) {
      return relay::hop_message::message_kind::connect;
   }
   if (value == static_cast<std::uint16_t>(relay::hop_message::message_kind::status)) {
      return relay::hop_message::message_kind::status;
   }
   exceptions::raise(exceptions::code::codec_error, "unknown relay hop message type");
}

[[nodiscard]] relay::stop_message::message_kind checked_stop_kind(std::uint64_t value) {
   if (value == static_cast<std::uint16_t>(relay::stop_message::message_kind::connect)) {
      return relay::stop_message::message_kind::connect;
   }
   if (value == static_cast<std::uint16_t>(relay::stop_message::message_kind::status)) {
      return relay::stop_message::message_kind::status;
   }
   exceptions::raise(exceptions::code::codec_error, "unknown relay stop message type");
}

[[nodiscard]] relay::status checked_status(std::uint64_t value) {
   switch (static_cast<relay::status>(value)) {
   case relay::status::unused:
   case relay::status::ok:
   case relay::status::reservation_refused:
   case relay::status::resource_limit_exceeded:
   case relay::status::permission_denied:
   case relay::status::connection_failed:
   case relay::status::no_reservation:
   case relay::status::malformed_message:
   case relay::status::unexpected_message:
      return static_cast<relay::status>(value);
   }
   exceptions::raise(exceptions::code::codec_error, "unknown relay status");
}

} // namespace

std::vector<std::uint8_t> make_hop_payload(const relay::hop_message& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.kind));
   if (value.target) {
      append_peer(out, 2, *value.target);
   }
   if (value.reservation_value) {
      append_reservation(out, 3, *value.reservation_value);
   }
   if (value.limit_value) {
      append_limit(out, 4, *value.limit_value);
   }
   if (value.status != relay::status::unused || value.kind == relay::hop_message::message_kind::status) {
      detail::append_uint64(out, 5, static_cast<std::uint16_t>(value.status));
   }
   return out;
}

std::vector<std::uint8_t> relay::codec::encode_hop(const relay::hop_message& value) {
   return detail::wrap_message(make_hop_payload(value));
}

relay::hop_message read_hop_payload(std::span<const std::uint8_t> bytes) {
   auto out = relay::hop_message{};
   auto in = detail::reader{bytes};
   auto saw_type = false;
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay hop type must be varint");
         }
         out.kind = checked_hop_kind(in.read_varint());
         saw_type = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay hop peer must be bytes");
         }
         out.target = decode_peer(in.bytes());
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay hop reservation must be bytes");
         }
         out.reservation_value = decode_reservation(in.bytes());
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay hop limit must be bytes");
         }
         out.limit_value = decode_limit(in.bytes());
         break;
      case 5:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay hop status must be varint");
         }
         out.status = checked_status(in.read_varint());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      exceptions::raise(exceptions::code::codec_error, "relay hop message is missing required type");
   }
   return out;
}

relay::hop_message relay::codec::decode_hop(std::span<const std::uint8_t> bytes, std::size_t max_message_size) {
   return read_hop_payload(detail::unwrap_message(bytes, max_message_size));
}

std::vector<std::uint8_t> make_stop_payload(const relay::stop_message& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.kind));
   if (value.source) {
      append_peer(out, 2, *value.source);
   }
   if (value.limit_value) {
      append_limit(out, 3, *value.limit_value);
   }
   if (value.status != relay::status::unused || value.kind == relay::stop_message::message_kind::status) {
      detail::append_uint64(out, 4, static_cast<std::uint16_t>(value.status));
   }
   return out;
}

std::vector<std::uint8_t> relay::codec::encode_stop(const relay::stop_message& value) {
   return detail::wrap_message(make_stop_payload(value));
}

relay::stop_message read_stop_payload(std::span<const std::uint8_t> bytes) {
   auto out = relay::stop_message{};
   auto in = detail::reader{bytes};
   auto saw_type = false;
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay stop type must be varint");
         }
         out.kind = checked_stop_kind(in.read_varint());
         saw_type = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay stop peer must be bytes");
         }
         out.source = decode_peer(in.bytes());
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay stop limit must be bytes");
         }
         out.limit_value = decode_limit(in.bytes());
         break;
      case 4:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay stop status must be varint");
         }
         out.status = checked_status(in.read_varint());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      exceptions::raise(exceptions::code::codec_error, "relay stop message is missing required type");
   }
   return out;
}

relay::stop_message relay::codec::decode_stop(std::span<const std::uint8_t> bytes, std::size_t max_message_size) {
   return read_stop_payload(detail::unwrap_message(bytes, max_message_size));
}

std::vector<std::uint8_t> make_reservation_voucher_payload(const relay::voucher& value) {
   if (!valid_peer_id(value.relay_peer) || !valid_peer_id(value.peer) || value.expires_at == 0) {
      exceptions::raise(exceptions::code::invalid_options, "invalid relay reservation voucher");
   }
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.relay_peer.to_bytes());
   detail::append_bytes(out, 2, value.peer.to_bytes());
   detail::append_uint64(out, 3, value.expires_at);
   return out;
}

relay::voucher read_reservation_voucher_payload(std::span<const std::uint8_t> bytes) {
   if (bytes.empty()) {
      exceptions::raise(exceptions::code::codec_error, "relay voucher is empty");
   }
   auto out = relay::voucher{};
   auto saw_relay = false;
   auto saw_peer = false;
   auto saw_expiration = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay voucher relay id must be bytes");
         }
         out.relay_peer = peer_id::from_bytes(in.bytes());
         saw_relay = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "relay voucher peer id must be bytes");
         }
         out.peer = peer_id::from_bytes(in.bytes());
         saw_peer = true;
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "relay voucher expiration must be varint");
         }
         out.expires_at = in.read_varint();
         saw_expiration = true;
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_relay || !saw_peer || !saw_expiration || out.expires_at == 0) {
      exceptions::raise(exceptions::code::codec_error, "relay voucher missing required fields");
   }
   return out;
}

std::vector<std::uint8_t> relay::codec::reservation_voucher_payload_type() {
   return {0x03, 0x02};
}

signed_envelope relay::codec::seal_reservation_voucher(const relay::voucher& value, const public_key& key,
                                                       const fcl::crypto::private_key& private_key) {
   const auto payload = make_reservation_voucher_payload(value);
   return signed_envelope::seal(key, private_key, "libp2p-relay-rsvp", reservation_voucher_payload_type(), payload);
}

relay::voucher relay::codec::open_reservation_voucher(const signed_envelope& envelope, const peer_id& relay_peer,
                                                       std::uint64_t now_unix_seconds) {
   envelope.verify("libp2p-relay-rsvp", relay_peer);
   if (envelope.payload_type != reservation_voucher_payload_type()) {
      exceptions::raise(exceptions::code::codec_error, "relay reservation voucher payload type mismatch");
   }
   auto out = read_reservation_voucher_payload(envelope.payload);
   if (out.relay_peer != relay_peer) {
      exceptions::raise(exceptions::code::invalid_identity, "relay reservation voucher relay mismatch");
   }
   if (out.expires_at <= now_unix_seconds) {
      exceptions::raise(exceptions::code::timeout, "relay reservation voucher is expired");
   }
   return out;
}

} // namespace fcl::p2p
