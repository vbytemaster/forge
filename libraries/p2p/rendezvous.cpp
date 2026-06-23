module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.p2p.rendezvous;

import forge.multiformats.exceptions;
import forge.multiformats.multiaddr;
import forge.multiformats.varint;
import forge.p2p.exceptions;

#include "protobuf.hpp"

namespace forge::p2p {
namespace {

constexpr auto legacy_peer_record_domain = std::string_view{"libp2p-routing-state"};
constexpr auto legacy_peer_record_payload_type = std::string_view{"/libp2p/routing-state-record"};

[[nodiscard]] std::vector<std::uint8_t> bytes_from_text(std::string_view value) {
   return {value.begin(), value.end()};
}

void validate_options(const rendezvous::options& opts) {
   if (opts.default_ttl.count() <= 0 || opts.min_ttl.count() <= 0 || opts.max_ttl.count() <= 0 ||
       opts.min_ttl > opts.max_ttl || opts.max_namespace_size == 0 || opts.max_registrations_per_peer == 0 ||
       opts.max_discover_limit == 0 || opts.max_message_size == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid rendezvous options");
   }
}

void validate_namespace(std::string_view value, const rendezvous::options& opts) {
   if (value.size() > opts.max_namespace_size) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous namespace exceeds max size");
   }
}

[[nodiscard]] rendezvous::message_type checked_message_type(std::uint64_t value) {
   switch (static_cast<rendezvous::message_type>(value)) {
   case rendezvous::message_type::register_peer:
   case rendezvous::message_type::register_response:
   case rendezvous::message_type::unregister_peer:
   case rendezvous::message_type::discover:
   case rendezvous::message_type::discover_response:
      return static_cast<rendezvous::message_type>(value);
   }
   FORGE_THROW_EXCEPTION(exceptions::codec_error, "unknown rendezvous message type");
}

[[nodiscard]] rendezvous::status checked_status(std::uint64_t value) {
   switch (static_cast<rendezvous::status>(value)) {
   case rendezvous::status::ok:
   case rendezvous::status::invalid_namespace:
   case rendezvous::status::invalid_signed_peer_record:
   case rendezvous::status::invalid_ttl:
   case rendezvous::status::invalid_cookie:
   case rendezvous::status::not_authorized:
   case rendezvous::status::internal_error:
   case rendezvous::status::unavailable:
      return static_cast<rendezvous::status>(value);
   }
   FORGE_THROW_EXCEPTION(exceptions::codec_error, "unknown rendezvous response status");
}

[[nodiscard]] std::vector<std::uint8_t> encode_register(const rendezvous::register_request& value,
                                                        const rendezvous::options& opts) {
   validate_namespace(value.namespace_name, opts);
   if (opts.require_signed_peer_record && value.signed_peer_record.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "rendezvous registration requires signed peer record");
   }
   if (value.ttl.count() > 0 && (value.ttl < opts.min_ttl || value.ttl > opts.max_ttl)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "rendezvous registration TTL outside allowed range");
   }
   auto out = std::vector<std::uint8_t>{};
   if (!value.namespace_name.empty()) {
      detail::append_string(out, 1, value.namespace_name);
   }
   if (!value.signed_peer_record.empty()) {
      detail::append_bytes(out, 2, value.signed_peer_record);
   }
   if (value.ttl.count() > 0) {
      detail::append_uint64(out, 3, static_cast<std::uint64_t>(value.ttl.count()));
   }
   return out;
}

[[nodiscard]] rendezvous::register_request decode_register(std::span<const std::uint8_t> bytes,
                                                           const rendezvous::options& opts) {
   auto out = rendezvous::register_request{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous signed peer record must be bytes");
         }
         out.signed_peer_record = in.bytes();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous registration TTL must be varint");
         }
         out.ttl = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (opts.require_signed_peer_record && out.signed_peer_record.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous registration missing signed peer record");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_register_response(const rendezvous::register_response& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.status_value));
   if (!value.status_text.empty()) {
      detail::append_string(out, 2, value.status_text);
   }
   if (value.ttl.count() > 0) {
      detail::append_uint64(out, 3, static_cast<std::uint64_t>(value.ttl.count()));
   }
   return out;
}

[[nodiscard]] rendezvous::register_response decode_register_response(std::span<const std::uint8_t> bytes) {
   auto out = rendezvous::register_response{};
   auto saw_status = false;
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous status must be varint");
         }
         out.status_value = checked_status(in.read_varint());
         saw_status = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous status text must be bytes");
         }
         out.status_text = in.string();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous response TTL must be varint");
         }
         out.ttl = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_status) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous response missing status");
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_unregister(const rendezvous::unregister_request& value,
                                                          const rendezvous::options& opts) {
   validate_namespace(value.namespace_name, opts);
   auto out = std::vector<std::uint8_t>{};
   if (!value.namespace_name.empty()) {
      detail::append_string(out, 1, value.namespace_name);
   }
   if (value.peer) {
      detail::append_bytes(out, 2, value.peer->to_bytes());
   }
   return out;
}

[[nodiscard]] rendezvous::unregister_request decode_unregister(std::span<const std::uint8_t> bytes,
                                                               const rendezvous::options& opts) {
   auto out = rendezvous::unregister_request{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous unregister id must be bytes");
         }
         out.peer = peer_id::from_bytes(in.bytes());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_discover(const rendezvous::discover_request& value,
                                                        const rendezvous::options& opts) {
   validate_namespace(value.namespace_name, opts);
   if (value.limit > opts.max_discover_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "rendezvous discover limit exceeds max");
   }
   auto out = std::vector<std::uint8_t>{};
   if (!value.namespace_name.empty()) {
      detail::append_string(out, 1, value.namespace_name);
   }
   if (value.limit != 0) {
      detail::append_uint64(out, 2, value.limit);
   }
   if (!value.cookie.empty()) {
      detail::append_bytes(out, 3, value.cookie);
   }
   return out;
}

[[nodiscard]] rendezvous::discover_request decode_discover(std::span<const std::uint8_t> bytes,
                                                           const rendezvous::options& opts) {
   auto out = rendezvous::discover_request{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous discover limit must be varint");
         }
         out.limit = static_cast<std::size_t>(in.read_varint());
         if (out.limit > opts.max_discover_limit) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous discover limit exceeds max");
         }
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous discover cookie must be bytes");
         }
         out.cookie = in.bytes();
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_discover_response(const rendezvous::discover_response& value,
                                                                 const rendezvous::options& opts) {
   auto out = std::vector<std::uint8_t>{};
   if (value.registrations.size() > opts.max_discover_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "rendezvous response has too many registrations");
   }
   for (const auto& registration : value.registrations) {
      detail::append_bytes(out, 1, encode_register(rendezvous::register_request{
                                      .namespace_name = registration.namespace_name,
                                      .signed_peer_record = registration.signed_peer_record,
                                      .ttl = registration.ttl,
                                  },
                                  opts));
   }
   if (!value.cookie.empty()) {
      detail::append_bytes(out, 2, value.cookie);
   }
   detail::append_uint64(out, 3, static_cast<std::uint16_t>(value.status_value));
   if (!value.status_text.empty()) {
      detail::append_string(out, 4, value.status_text);
   }
   return out;
}

[[nodiscard]] rendezvous::discover_response decode_discover_response(std::span<const std::uint8_t> bytes,
                                                                     const rendezvous::options& opts) {
   auto out = rendezvous::discover_response{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous registration must be bytes");
         }
         {
            const auto request = decode_register(in.bytes(), opts);
            auto registration = rendezvous::registration{
                .namespace_name = request.namespace_name,
                .signed_peer_record = request.signed_peer_record,
                .ttl = request.ttl,
            };
            if (!request.signed_peer_record.empty()) {
               const auto record = rendezvous::codec::open_peer_record(signed_envelope::decode(request.signed_peer_record));
               registration.peer = record.peer;
               registration.endpoints = record.endpoints;
               registration.sequence = record.sequence;
            }
            out.registrations.push_back(std::move(registration));
            if (out.registrations.size() > opts.max_discover_limit) {
               FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous response has too many registrations");
            }
         }
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous cookie must be bytes");
         }
         out.cookie = in.bytes();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous status must be varint");
         }
         out.status_value = checked_status(in.read_varint());
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous status text must be bytes");
         }
         out.status_text = in.string();
         break;
      default:
         in.skip(type);
         break;
      }
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_payload(const rendezvous::message& value,
                                                       const rendezvous::options& opts) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.type));
   if (value.register_value) {
      detail::append_bytes(out, 2, encode_register(*value.register_value, opts));
   }
   if (value.register_response_value) {
      detail::append_bytes(out, 3, encode_register_response(*value.register_response_value));
   }
   if (value.unregister_value) {
      detail::append_bytes(out, 4, encode_unregister(*value.unregister_value, opts));
   }
   if (value.discover_value) {
      detail::append_bytes(out, 5, encode_discover(*value.discover_value, opts));
   }
   if (value.discover_response_value) {
      detail::append_bytes(out, 6, encode_discover_response(*value.discover_response_value, opts));
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> encode_peer_record_address(const endpoint& value) {
   const auto address = forge::multiformats::multiaddr::parse(value.to_string());
   return address.to_bytes();
}

[[nodiscard]] std::optional<endpoint> decode_peer_record_address(std::span<const std::uint8_t> bytes,
                                                                 const peer_id& peer) {
   try {
      auto out = parse_endpoint(forge::multiformats::multiaddr::from_bytes(bytes).to_string());
      if (!out.peer) {
         out.peer = peer;
      }
      return out;
   } catch (const forge::exceptions::base&) {
      return std::nullopt;
   }
}

[[nodiscard]] std::vector<std::uint8_t> encode_address_info(const endpoint& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, encode_peer_record_address(value));
   return out;
}

[[nodiscard]] std::optional<endpoint> decode_address_info(std::span<const std::uint8_t> bytes, const peer_id& peer) {
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      if (field == 1) {
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record address must be bytes");
         }
         return decode_peer_record_address(in.bytes(), peer);
      }
      in.skip(type);
   }
   return std::nullopt;
}

} // namespace

std::vector<std::uint8_t> rendezvous::codec::encode(const rendezvous::message& value) {
   return encode(value, rendezvous::options{});
}

std::vector<std::uint8_t> rendezvous::codec::encode(const rendezvous::message& value,
                                                    const rendezvous::options& opts) {
   validate_options(opts);
   return detail::wrap_message(encode_payload(value, opts));
}

rendezvous::message rendezvous::codec::decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, rendezvous::options{});
}

rendezvous::message rendezvous::codec::decode(std::span<const std::uint8_t> bytes,
                                              const rendezvous::options& opts) {
   validate_options(opts);
   const auto payload = detail::unwrap_message(bytes, opts.max_message_size);
   auto out = rendezvous::message{};
   auto saw_type = false;
   auto in = detail::reader{payload};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous message type must be varint");
         }
         out.type = checked_message_type(in.read_varint());
         saw_type = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous register must be bytes");
         }
         out.register_value = decode_register(in.bytes(), opts);
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous register response must be bytes");
         }
         out.register_response_value = decode_register_response(in.bytes());
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous unregister must be bytes");
         }
         out.unregister_value = decode_unregister(in.bytes(), opts);
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous discover must be bytes");
         }
         out.discover_value = decode_discover(in.bytes(), opts);
         break;
      case 6:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous discover response must be bytes");
         }
         out.discover_response_value = decode_discover_response(in.bytes(), opts);
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous message missing type");
   }
   return out;
}

std::vector<std::uint8_t> rendezvous::codec::make_cookie(std::uint64_t sequence) {
   return make_cookie(sequence, {});
}

std::vector<std::uint8_t> rendezvous::codec::make_cookie(std::uint64_t sequence, std::string_view namespace_name) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(8 + namespace_name.size());
   for (auto shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>((sequence >> shift) & 0xffU));
   }
   out.insert(out.end(), namespace_name.begin(), namespace_name.end());
   return out;
}

std::uint64_t rendezvous::codec::read_cookie(std::span<const std::uint8_t> cookie) {
   if (cookie.empty()) {
      return 0;
   }
   if (cookie.size() < 8) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid rendezvous cookie");
   }
   auto out = std::uint64_t{};
   for (auto i = std::size_t{}; i < 8; ++i) {
      out = (out << 8U) | cookie[i];
   }
   return out;
}

std::string rendezvous::codec::read_cookie_namespace(std::span<const std::uint8_t> cookie) {
   if (cookie.empty()) {
      return {};
   }
   if (cookie.size() < 8) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "invalid rendezvous cookie");
   }
   return {cookie.begin() + 8, cookie.end()};
}

std::vector<std::uint8_t> rendezvous::codec::encode_peer_record(const rendezvous::peer_record& value) {
   if (!valid_peer_id(value.peer)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "rendezvous peer record has invalid peer id");
   }
   auto out = std::vector<std::uint8_t>{};
   detail::append_bytes(out, 1, value.peer.to_bytes());
   detail::append_uint64(out, 2, value.sequence);
   for (const auto& address : value.endpoints) {
      detail::append_bytes(out, 3, encode_address_info(address));
   }
   return out;
}

rendezvous::peer_record rendezvous::codec::decode_peer_record(std::span<const std::uint8_t> bytes) {
   auto out = rendezvous::peer_record{};
   auto saw_peer = false;
   auto address_infos = std::vector<std::vector<std::uint8_t>>{};
   auto in = detail::reader{bytes};
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record id must be bytes");
         }
         out.peer = peer_id::from_bytes(in.bytes());
         saw_peer = true;
         break;
      case 2:
         if (type != detail::wire_type::varint) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record sequence must be varint");
         }
         out.sequence = in.read_varint();
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record address info must be bytes");
         }
         address_infos.push_back(in.bytes());
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_peer) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record missing peer id");
   }
   for (const auto& value : address_infos) {
      const auto address = decode_address_info(value, out.peer);
      if (address) {
         out.endpoints.push_back(*address);
      }
   }
   return out;
}

std::vector<std::uint8_t> rendezvous::codec::peer_record_payload_type() {
   return bytes_from_text(legacy_peer_record_payload_type);
}

signed_envelope rendezvous::codec::seal_peer_record(const rendezvous::peer_record& value, const public_key& key,
                                                    const forge::crypto::asymmetric::private_key& private_key) {
   const auto payload = encode_peer_record(value);
   const auto payload_type = peer_record_payload_type();
   return signed_envelope::seal(key, private_key, legacy_peer_record_domain, payload_type, payload);
}

rendezvous::peer_record rendezvous::codec::open_peer_record(const signed_envelope& envelope,
                                                           std::optional<peer_id> expected_signer) {
   envelope.verify(legacy_peer_record_domain, expected_signer);
   if (envelope.payload_type != peer_record_payload_type()) {
      FORGE_THROW_EXCEPTION(exceptions::codec_error, "rendezvous peer record has unsupported payload type");
   }
   auto out = decode_peer_record(envelope.payload);
   if (expected_signer && out.peer != *expected_signer) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, "rendezvous peer record peer id mismatch");
   }
   return out;
}

} // namespace forge::p2p
