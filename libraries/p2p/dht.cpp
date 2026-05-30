module;

#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module fcl.p2p.dht;

import fcl.crypto.sha256;
import fcl.multiformats.multiaddr;
import fcl.multiformats.exceptions;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.p2p.exceptions;

#include "protobuf.hpp"

namespace fcl::p2p {
namespace {

[[nodiscard]] std::vector<std::uint8_t> endpoint_bytes(const endpoint& value) {
   return fcl::multiformats::multiaddr::parse(value.to_string()).to_bytes();
}

[[nodiscard]] endpoint endpoint_from_bytes(std::span<const std::uint8_t> value) {
   return parse_endpoint(fcl::multiformats::multiaddr::from_bytes(value).to_string());
}

void validate_options(const dht::options& opts) {
   if (opts.replication == 0 || opts.alpha == 0 || opts.max_message_size == 0 || opts.max_record_size == 0 ||
       opts.max_closer_peers == 0 || opts.max_provider_peers == 0 || opts.query_timeout.count() <= 0 ||
       opts.refresh_interval.count() <= 0 || opts.provider_record_ttl.count() <= 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid DHT options");
   }
}

[[nodiscard]] dht::message_type checked_message_type(std::uint64_t value) {
   switch (static_cast<dht::message_type>(value)) {
   case dht::message_type::put_value:
   case dht::message_type::get_value:
   case dht::message_type::add_provider:
   case dht::message_type::get_providers:
   case dht::message_type::find_node:
   case dht::message_type::ping:
      return static_cast<dht::message_type>(value);
   }
   FCL_THROW_EXCEPTION(exceptions::codec_error, "unknown DHT message type");
}

[[nodiscard]] dht::connection_type checked_connection_type(std::uint64_t value) {
   switch (static_cast<dht::connection_type>(value)) {
   case dht::connection_type::not_connected:
   case dht::connection_type::connected:
   case dht::connection_type::can_connect:
   case dht::connection_type::cannot_connect:
      return static_cast<dht::connection_type>(value);
   }
   FCL_THROW_EXCEPTION(exceptions::codec_error, "unknown DHT peer connection type");
}

[[nodiscard]] std::vector<std::uint8_t> encode_record_payload(const dht::record& value, const dht::options& opts) {
   if (value.key_value.bytes.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "DHT record key must not be empty");
   }
   if (value.value.size() > opts.max_record_size) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "DHT record exceeds max size");
   }
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.key_value.bytes);
   detail::append_bytes(out, 2, value.value);
   if (!value.time_received.empty()) {
      detail::append_string(out, 5, value.time_received);
   }
   if (value.publisher) {
      detail::append_bytes(out, 666, value.publisher->to_bytes());
   }
   if (value.ttl.count() > 0) {
      detail::append_uint64(out, 777, static_cast<std::uint64_t>(value.ttl.count()));
   }
   return out;
}

[[nodiscard]] dht::record decode_record_payload(std::span<const std::uint8_t> bytes, const dht::options& opts) {
   auto out = dht::record{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record key must be bytes");
         }
         out.key_value = dht::key{.bytes = in.bytes()};
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record value must be bytes");
         }
         out.value = in.bytes();
         if (out.value.size() > opts.max_record_size) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record exceeds max size");
         }
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record timeReceived must be bytes");
         }
         out.time_received = in.string();
         break;
      case 666:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record publisher must be bytes");
         }
         out.publisher = peer_id::from_bytes(in.bytes());
         break;
      case 777:
         if (type != detail::wire_type::varint) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record ttl must be varint");
         }
         out.ttl = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (out.key_value.bytes.empty()) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record missing key");
   }
   return out;
}

void append_peer(std::vector<std::uint8_t>& out, std::uint32_t field, const dht::peer& value) {
   if (!valid_peer_id(value.id)) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "DHT peer has invalid Peer ID");
   }
   auto encoded = std::vector<std::uint8_t>{};
   detail::append_bytes(encoded, 1, value.id.to_bytes());
   for (const auto& endpoint : value.endpoints) {
      detail::append_bytes(encoded, 2, endpoint_bytes(endpoint));
   }
   detail::append_uint64(encoded, 3, static_cast<std::uint16_t>(value.connection));
   detail::append_bytes(out, field, encoded);
}

[[nodiscard]] dht::peer decode_peer(std::span<const std::uint8_t> bytes) {
   auto out = dht::peer{};
   auto saw_id = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT peer id must be bytes");
         }
         out.id = peer_id::from_bytes(in.bytes());
         saw_id = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT peer address must be bytes");
         }
         out.endpoints.push_back(endpoint_from_bytes(in.bytes()));
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT peer connection type must be varint");
         }
         out.connection = checked_connection_type(in.read_varint());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_id) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT peer missing id");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_payload(const dht::message& value, const dht::options& opts) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.type));
   if (value.cluster_level_raw != 0) {
      detail::append_uint64(out, 10, static_cast<std::uint64_t>(value.cluster_level_raw));
   }
   if (!value.key_value.bytes.empty()) {
      detail::append_bytes(out, 2, value.key_value.bytes);
   }
   if (value.record_value) {
      const auto record = encode_record_payload(*value.record_value, opts);
      detail::append_bytes(out, 3, record);
   }
   if (value.closer_peers.size() > opts.max_closer_peers) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "DHT message has too many closer peers");
   }
   for (const auto& peer : value.closer_peers) {
      append_peer(out, 8, peer);
   }
   if (value.provider_peers.size() > opts.max_provider_peers) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "DHT message has too many provider peers");
   }
   for (const auto& peer : value.provider_peers) {
      append_peer(out, 9, peer);
   }
   return out;
}

} // namespace

std::vector<std::uint8_t> dht::codec::encode(const dht::message& value) {
   return encode(value, dht::options{});
}

std::vector<std::uint8_t> dht::codec::encode(const dht::message& value, const dht::options& opts) {
   validate_options(opts);
   return detail::wrap_message(encode_payload(value, opts));
}

dht::message dht::codec::decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, dht::options{});
}

dht::message dht::codec::decode(std::span<const std::uint8_t> bytes, const dht::options& opts) {
   validate_options(opts);
   const auto payload = detail::unwrap_message(bytes, opts.max_message_size);
   auto out = dht::message{};
   auto saw_type = false;
   auto in = detail::reader{payload};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT message type must be varint");
         }
         out.type = checked_message_type(in.read_varint());
         saw_type = true;
         break;
      case 10:
         if (type != detail::wire_type::varint) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT cluster level must be varint");
         }
         out.cluster_level_raw = static_cast<std::int32_t>(in.read_varint());
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT key must be bytes");
         }
         out.key_value = dht::key{.bytes = in.bytes()};
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT record must be bytes");
         }
         out.record_value = decode_record_payload(in.bytes(), opts);
         break;
      case 8:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT closer peer must be bytes");
         }
         out.closer_peers.push_back(decode_peer(in.bytes()));
         if (out.closer_peers.size() > opts.max_closer_peers) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT message has too many closer peers");
         }
         break;
      case 9:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT provider peer must be bytes");
         }
         out.provider_peers.push_back(decode_peer(in.bytes()));
         if (out.provider_peers.size() > opts.max_provider_peers) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT message has too many provider peers");
         }
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "DHT message missing type");
   }
   return out;
}

struct dht::routing_table::impl {
   peer_id local;
   dht::options options;
   std::map<peer_id, dht::peer> peers;
   std::map<peer_id, std::uint64_t> failures;
};

dht::routing_table::routing_table(peer_id local_peer, dht::options options_value)
    : impl_(std::make_unique<impl>(impl{.local = std::move(local_peer), .options = std::move(options_value)})) {
   validate_options(impl_->options);
}

dht::routing_table::~routing_table() = default;
dht::routing_table::routing_table(routing_table&&) noexcept = default;
dht::routing_table& dht::routing_table::operator=(routing_table&&) noexcept = default;

void dht::routing_table::upsert(peer value) {
   if (!valid_peer_id(value.id) || value.id == impl_->local) {
      return;
   }
   impl_->peers[value.id] = std::move(value);
}

void dht::routing_table::mark_failure(const peer_id& peer) {
   ++impl_->failures[peer];
}

std::vector<dht::peer> dht::routing_table::closest(std::span<const std::uint8_t> target, std::size_t limit) const {
   auto entries = std::vector<std::pair<dht::distance, dht::peer>>{};
   entries.reserve(impl_->peers.size());
   for (const auto& [id, peer] : impl_->peers) {
      entries.push_back({distance_between(id.to_bytes(), target), peer});
   }
   std::ranges::sort(entries, [](const auto& left, const auto& right) {
      if (left.first != right.first) {
         return left.first < right.first;
      }
      return left.second.id < right.second.id;
   });
   const auto count = std::min({limit, impl_->options.replication, entries.size()});
   auto out = std::vector<dht::peer>{};
   out.reserve(count);
   for (auto i = std::size_t{}; i < count; ++i) {
      out.push_back(std::move(entries[i].second));
   }
   return out;
}

std::vector<dht::peer> dht::routing_table::snapshot() const {
   auto out = std::vector<dht::peer>{};
   out.reserve(impl_->peers.size());
   for (const auto& [_, peer] : impl_->peers) {
      out.push_back(peer);
   }
   return out;
}

dht::key make_dht_key(std::span<const std::uint8_t> value) {
   return dht::key{.bytes = std::vector<std::uint8_t>{value.begin(), value.end()}};
}

dht::key make_dht_key(const peer_id& peer) {
   return dht::key{.bytes = peer.to_bytes()};
}

dht::distance distance_between(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) {
   const auto left_hash = fcl::crypto::sha256::hash(left).to_uint8_span();
   const auto right_hash = fcl::crypto::sha256::hash(right).to_uint8_span();
   auto out = dht::distance{};
   for (auto i = std::size_t{}; i < out.bytes.size(); ++i) {
      out.bytes[i] = static_cast<std::uint8_t>(left_hash[i] ^ right_hash[i]);
   }
   return out;
}

} // namespace fcl::p2p
