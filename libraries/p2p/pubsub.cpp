module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.p2p.pubsub;

import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.multiformats.multicodec;
import forge.multiformats.exceptions;
import forge.multiformats.multihash;
import forge.multiformats.varint;
import forge.p2p.exceptions;
import forge.p2p.identity;

#include "identity_signature.hpp"
#include "protobuf.hpp"

namespace forge::p2p::pubsub {
namespace {

constexpr auto signing_prefix = std::string_view{"libp2p-pubsub:"};

void validate_options(const options& opts) {
   const auto& limits = opts.limits;
   if (limits.max_rpc_size == 0 || limits.max_message_size == 0 || limits.max_data_size == 0 ||
       limits.max_topic_size == 0 || limits.max_subscriptions == 0 || limits.max_messages == 0 ||
       limits.max_control_entries == 0 || limits.max_message_ids == 0 || limits.max_peers_per_topic == 0 ||
       limits.max_topics == 0 || limits.max_validation_queue == 0 || limits.max_outbound_queue_bytes == 0 ||
       limits.max_ihave_per_peer == 0 || limits.max_iwant_per_peer == 0 || limits.max_graft_per_peer == 0 ||
       limits.heartbeat_initial_delay.count() <= 0 || limits.heartbeat_interval.count() <= 0 ||
       limits.fanout_ttl.count() <= 0 || limits.prune_backoff.count() <= 0 ||
       limits.unsubscribe_backoff.count() <= 0 || limits.mesh_n == 0 || limits.mesh_n_low == 0 ||
       limits.mesh_n_high < limits.mesh_n_low || limits.history_length == 0 || limits.history_gossip == 0 ||
       limits.gossip_lazy == 0 || limits.gossip_factor <= 0.0 || limits.gossip_retransmission == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid GossipSub options");
   }
}

void validate_topic(const topic& value, const options& opts) {
   if (value.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub topic must not be empty");
   }
   if (value.value.size() > opts.limits.max_topic_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub topic exceeds max size");
   }
}

[[nodiscard]] std::vector<std::uint8_t> digest_bytes(const forge::crypto::sha256& digest) {
   const auto span = digest.to_uint8_span();
   return {span.begin(), span.end()};
}

void append_bool(std::vector<std::uint8_t>& out, std::uint32_t field, bool value) {
   detail::append_uint64(out, field, value ? 1U : 0U);
}

[[nodiscard]] std::vector<std::uint8_t> encode_subscription_payload(const subscription& value, const options& opts) {
   validate_topic(value.subject, opts);
   auto out = std::vector<std::uint8_t>{};
   append_bool(out, 1, value.subscribe);
   detail::append_string(out, 2, value.subject.value);
   return out;
}

[[nodiscard]] subscription decode_subscription_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = subscription{};
   auto saw_topic = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub subscription flag must be varint");
         }
         out.subscribe = in.read_varint() != 0;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub subscription topic must be bytes");
         }
         out.subject.value = in.string();
         saw_topic = true;
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub subscription is missing topic");
   }
   validate_topic(out.subject, opts);
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_message_payload(const message& value, const options& opts,
                                                               bool include_signature_key) {
   validate_topic(value.subject, opts);
   if (value.data.size() > opts.limits.max_data_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub message exceeds max data size");
   }
   auto out = std::vector<std::uint8_t>{};
   if (value.from) {
      detail::append_bytes(out, 1, value.from->to_bytes());
   }
   if (!value.data.empty()) {
      detail::append_bytes(out, 2, value.data);
   }
   if (!value.seqno.empty()) {
      detail::append_bytes(out, 3, value.seqno);
   }
   detail::append_string(out, 4, value.subject.value);
   if (include_signature_key && !value.signature.empty()) {
      detail::append_bytes(out, 5, value.signature);
   }
   if (include_signature_key && !value.key.empty()) {
      detail::append_bytes(out, 6, value.key);
   }
   if (out.size() > opts.limits.max_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub message exceeds max size");
   }
   return out;
}

[[nodiscard]] message decode_message_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   if (bytes.size() > opts.limits.max_message_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message exceeds max size");
   }
   auto out = message{};
   auto saw_topic = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message source must be bytes");
         }
         out.from = peer_id::from_bytes(in.bytes());
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message data must be bytes");
         }
         out.data = in.bytes();
         if (out.data.size() > opts.limits.max_data_size) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message exceeds max data size");
         }
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message seqno must be bytes");
         }
         out.seqno = in.bytes();
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message topic must be bytes");
         }
         out.subject.value = in.string();
         saw_topic = true;
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message signature must be bytes");
         }
         out.signature = in.bytes();
         break;
      case 6:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message key must be bytes");
         }
         out.key = in.bytes();
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub message is missing topic");
   }
   validate_topic(out.subject, opts);
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_peer_payload(const peer_info& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.peer.to_bytes());
   if (!value.signed_peer_record.empty()) {
      detail::append_bytes(out, 2, value.signed_peer_record);
   }
   return out;
}

[[nodiscard]] peer_info decode_peer_payload(std::span<const std::uint8_t> bytes) {
   auto out = peer_info{};
   auto saw_peer = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub peer id must be bytes");
         }
         out.peer = peer_id::from_bytes(in.bytes());
         saw_peer = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub signed peer record must be bytes");
         }
         out.signed_peer_record = in.bytes();
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_peer) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub peer info is missing peer id");
   }
   return out;
}

void append_message_ids(std::vector<std::uint8_t>& out, std::uint32_t field,
                        const std::vector<std::vector<std::uint8_t>>& ids, const options& opts) {
   if (ids.size() > opts.limits.max_message_ids) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub control has too many message ids");
   }
   for (const auto& id : ids) {
      if (id.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub message id must not be empty");
      }
      detail::append_bytes(out, field, id);
   }
}

[[nodiscard]] std::vector<std::uint8_t> encode_ihave_payload(const control::ihave& value, const options& opts) {
   validate_topic(value.subject, opts);
   auto out = std::vector<std::uint8_t>{};
   detail::append_string(out, 1, value.subject.value);
   append_message_ids(out, 2, value.message_ids, opts);
   return out;
}

[[nodiscard]] control::ihave decode_ihave_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = control::ihave{};
   auto saw_topic = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IHAVE topic must be bytes");
         }
         out.subject.value = in.string();
         saw_topic = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IHAVE message id must be bytes");
         }
         out.message_ids.push_back(in.bytes());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IHAVE is missing topic");
   }
   validate_topic(out.subject, opts);
   if (out.message_ids.size() > opts.limits.max_message_ids) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IHAVE has too many message ids");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_iwant_payload(const control::iwant& value, const options& opts) {
   auto out = std::vector<std::uint8_t>{};
   append_message_ids(out, 1, value.message_ids, opts);
   return out;
}

[[nodiscard]] control::iwant decode_iwant_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = control::iwant{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IWANT message id must be bytes");
         }
         out.message_ids.push_back(in.bytes());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (out.message_ids.size() > opts.limits.max_message_ids) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub IWANT has too many message ids");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_graft_payload(const control::graft& value, const options& opts) {
   validate_topic(value.subject, opts);
   auto out = std::vector<std::uint8_t>{};
   detail::append_string(out, 1, value.subject.value);
   return out;
}

[[nodiscard]] control::graft decode_graft_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = control::graft{};
   auto saw_topic = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub GRAFT topic must be bytes");
         }
         out.subject.value = in.string();
         saw_topic = true;
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub GRAFT is missing topic");
   }
   validate_topic(out.subject, opts);
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_prune_payload(const control::prune& value, const options& opts) {
   validate_topic(value.subject, opts);
   if (value.peers.size() > opts.limits.max_peers_per_topic) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub PRUNE has too many peers");
   }
   auto out = std::vector<std::uint8_t>{};
   detail::append_string(out, 1, value.subject.value);
   for (const auto& peer : value.peers) {
      detail::append_bytes(out, 2, encode_peer_payload(peer));
   }
   if (value.backoff.count() > 0) {
      detail::append_uint64(out, 3, static_cast<std::uint64_t>(value.backoff.count()));
   }
   return out;
}

[[nodiscard]] control::prune decode_prune_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = control::prune{};
   auto saw_topic = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub PRUNE topic must be bytes");
         }
         out.subject.value = in.string();
         saw_topic = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub PRUNE peer must be bytes");
         }
         out.peers.push_back(decode_peer_payload(in.bytes()));
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub PRUNE backoff must be varint");
         }
         out.backoff = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub PRUNE is missing topic");
   }
   validate_topic(out.subject, opts);
   if (out.peers.size() > opts.limits.max_peers_per_topic) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub PRUNE has too many peers");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_control_payload(const control& value, const options& opts) {
   const auto total = value.have.size() + value.want.size() + value.grafts.size() + value.prunes.size();
   if (total > opts.limits.max_control_entries) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub control message has too many entries");
   }
   auto out = std::vector<std::uint8_t>{};
   for (const auto& item : value.have) {
      detail::append_bytes(out, 1, encode_ihave_payload(item, opts));
   }
   for (const auto& item : value.want) {
      detail::append_bytes(out, 2, encode_iwant_payload(item, opts));
   }
   for (const auto& item : value.grafts) {
      detail::append_bytes(out, 3, encode_graft_payload(item, opts));
   }
   for (const auto& item : value.prunes) {
      detail::append_bytes(out, 4, encode_prune_payload(item, opts));
   }
   return out;
}

[[nodiscard]] control decode_control_payload(std::span<const std::uint8_t> bytes, const options& opts) {
   auto out = control{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (type != detail::wire_type::length_delimited) {
         FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub control entry must be bytes");
      }
      switch (field) {
      case 1:
         out.have.push_back(decode_ihave_payload(in.bytes(), opts));
         break;
      case 2:
         out.want.push_back(decode_iwant_payload(in.bytes(), opts));
         break;
      case 3:
         out.grafts.push_back(decode_graft_payload(in.bytes(), opts));
         break;
      case 4:
         out.prunes.push_back(decode_prune_payload(in.bytes(), opts));
         break;
      default:
         in.skip(type);
         break;
      }
   }
   const auto total = out.have.size() + out.want.size() + out.grafts.size() + out.prunes.size();
   if (total > opts.limits.max_control_entries) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub control message has too many entries");
   }
   return out;
}

[[nodiscard]] std::optional<public_key> public_key_from_message(const message& value) {
   if (!value.from) {
      return std::nullopt;
   }
   if (!value.key.empty()) {
      return decode_public_key(value.key);
   }
   const auto hash = forge::multiformats::multihash::decode(value.from->to_bytes());
   if (hash.code != forge::multiformats::code_value(forge::multiformats::multicodec_code::identity) ||
       hash.digest.empty()) {
      return std::nullopt;
   }
   return decode_public_key(hash.digest);
}

} // namespace

protocol_id codec::protocol(version value) {
   switch (value) {
   case version::v1_0:
      return builtins::meshsub_v10;
   case version::v1_1:
      return builtins::meshsub_v11;
   }
   return builtins::meshsub_v11;
}

std::vector<std::uint8_t> codec::encode(const rpc& value) {
   return encode(value, options{});
}

std::vector<std::uint8_t> codec::encode(const rpc& value, const options& opts) {
   validate_options(opts);
   if (value.subscriptions.size() > opts.limits.max_subscriptions) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub RPC has too many subscriptions");
   }
   if (value.messages.size() > opts.limits.max_messages) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub RPC has too many messages");
   }
   auto out = std::vector<std::uint8_t>{};
   for (const auto& item : value.subscriptions) {
      detail::append_bytes(out, 1, encode_subscription_payload(item, opts));
   }
   for (const auto& item : value.messages) {
      detail::append_bytes(out, 2, encode_message_payload(item, opts, true));
   }
   if (value.control_value) {
      detail::append_bytes(out, 3, encode_control_payload(*value.control_value, opts));
   }
   if (out.size() > opts.limits.max_rpc_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub RPC exceeds max size");
   }
   return detail::wrap_message(out);
}

rpc codec::decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, options{});
}

rpc codec::decode(std::span<const std::uint8_t> bytes, const options& opts) {
   validate_options(opts);
   const auto payload = detail::unwrap_message(bytes, opts.limits.max_rpc_size);
   if (payload.size() > opts.limits.max_rpc_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub RPC exceeds max size");
   }
   auto out = rpc{};
   auto in = detail::reader{payload};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub subscription must be bytes");
         }
         out.subscriptions.push_back(decode_subscription_payload(in.bytes(), opts));
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub publish message must be bytes");
         }
         out.messages.push_back(decode_message_payload(in.bytes(), opts));
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub control message must be bytes");
         }
         out.control_value = decode_control_payload(in.bytes(), opts);
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (out.subscriptions.size() > opts.limits.max_subscriptions || out.messages.size() > opts.limits.max_messages) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "GossipSub RPC exceeds element limits");
   }
   return out;
}

std::vector<std::uint8_t> codec::encode_message(const message& value) {
   return encode_message_payload(value, options{}, true);
}

std::vector<std::uint8_t> codec::signing_payload(const message& value) {
   auto out = std::vector<std::uint8_t>{signing_prefix.begin(), signing_prefix.end()};
   const auto encoded = encode_message_payload(value, options{}, false);
   out.insert(out.end(), encoded.begin(), encoded.end());
   return out;
}

std::vector<std::uint8_t> codec::message_id(const message& value) {
   if (value.from && !value.seqno.empty()) {
      auto out = value.from->to_bytes();
      out.insert(out.end(), value.seqno.begin(), value.seqno.end());
      return out;
   }
   const auto encoded = encode_message_payload(value, options{}, true);
   return digest_bytes(forge::crypto::sha256::hash(std::span<const std::uint8_t>{encoded}));
}

void codec::sign_message(message& value, const forge::crypto::asymmetric::private_key& key) {
   if (value.seqno.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "GossipSub signed message requires seqno");
   }
   const auto public_value = public_key_from_crypto(key.get_public_key());
   value.from = make_peer_id(public_value);
   value.key = encode_public_key(public_value);
   value.signature.clear();
   const auto payload = signing_payload(value);
   value.signature = sign_identity(key, payload);
}

bool codec::verify_message(const message& value) {
   if (!value.from || value.signature.empty() || value.seqno.empty()) {
      return false;
   }
   try {
      const auto key = public_key_from_message(value);
      if (!key || make_peer_id(*key) != *value.from) {
         return false;
      }
      const auto payload = signing_payload(value);
      return verify_identity_signature(*key, payload, value.signature);
   } catch (const forge::exceptions::base&) {
      return false;
   }
}

} // namespace forge::p2p::pubsub
