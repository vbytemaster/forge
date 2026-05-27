module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

module fcl.p2p.rendezvous;

import fcl.multiformats.exceptions;
import fcl.multiformats.varint;
import fcl.p2p.exceptions;

#include "protobuf.hpp"

namespace fcl::p2p {
namespace {

void validate_options(const rendezvous::options& opts) {
   if (opts.default_ttl.count() <= 0 || opts.min_ttl.count() <= 0 || opts.max_ttl.count() <= 0 ||
       opts.min_ttl > opts.max_ttl || opts.max_namespace_size == 0 || opts.max_registrations_per_peer == 0 ||
       opts.max_discover_limit == 0 || opts.max_message_size == 0) {
      exceptions::raise(exceptions::code::invalid_options, "invalid rendezvous options");
   }
}

void validate_namespace(std::string_view value, const rendezvous::options& opts) {
   if (value.size() > opts.max_namespace_size) {
      exceptions::raise(exceptions::code::codec_error, "rendezvous namespace exceeds max size");
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
   exceptions::raise(exceptions::code::codec_error, "unknown rendezvous message type");
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
   exceptions::raise(exceptions::code::codec_error, "unknown rendezvous response status");
}

[[nodiscard]] std::vector<std::uint8_t> encode_register(const rendezvous::register_request& value,
                                                        const rendezvous::options& opts) {
   validate_namespace(value.namespace_name, opts);
   if (opts.require_signed_peer_record && value.signed_peer_record.empty()) {
      exceptions::raise(exceptions::code::invalid_options, "rendezvous registration requires signed peer record");
   }
   if (value.ttl.count() > 0 && (value.ttl < opts.min_ttl || value.ttl > opts.max_ttl)) {
      exceptions::raise(exceptions::code::invalid_options, "rendezvous registration TTL outside allowed range");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous signed peer record must be bytes");
         }
         out.signed_peer_record = in.bytes();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous registration TTL must be varint");
         }
         out.ttl = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (opts.require_signed_peer_record && out.signed_peer_record.empty()) {
      exceptions::raise(exceptions::code::codec_error, "rendezvous registration missing signed peer record");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous status must be varint");
         }
         out.status_value = checked_status(in.read_varint());
         saw_status = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous status text must be bytes");
         }
         out.status_text = in.string();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous response TTL must be varint");
         }
         out.ttl = std::chrono::seconds{static_cast<std::int64_t>(in.read_varint())};
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_status) {
      exceptions::raise(exceptions::code::codec_error, "rendezvous response missing status");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous unregister id must be bytes");
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
      exceptions::raise(exceptions::code::invalid_options, "rendezvous discover limit exceeds max");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous namespace must be bytes");
         }
         out.namespace_name = in.string();
         validate_namespace(out.namespace_name, opts);
         break;
      case 2:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous discover limit must be varint");
         }
         out.limit = static_cast<std::size_t>(in.read_varint());
         if (out.limit > opts.max_discover_limit) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous discover limit exceeds max");
         }
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous discover cookie must be bytes");
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
      exceptions::raise(exceptions::code::invalid_options, "rendezvous response has too many registrations");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous registration must be bytes");
         }
         {
            const auto request = decode_register(in.bytes(), opts);
            out.registrations.push_back(rendezvous::registration{
                .namespace_name = request.namespace_name,
                .signed_peer_record = request.signed_peer_record,
                .ttl = request.ttl,
            });
            if (out.registrations.size() > opts.max_discover_limit) {
               exceptions::raise(exceptions::code::codec_error, "rendezvous response has too many registrations");
            }
         }
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous cookie must be bytes");
         }
         out.cookie = in.bytes();
         break;
      case 3:
         if (type != detail::wire_type::varint) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous status must be varint");
         }
         out.status_value = checked_status(in.read_varint());
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous status text must be bytes");
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
            exceptions::raise(exceptions::code::codec_error, "rendezvous message type must be varint");
         }
         out.type = checked_message_type(in.read_varint());
         saw_type = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous register must be bytes");
         }
         out.register_value = decode_register(in.bytes(), opts);
         break;
      case 3:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous register response must be bytes");
         }
         out.register_response_value = decode_register_response(in.bytes());
         break;
      case 4:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous unregister must be bytes");
         }
         out.unregister_value = decode_unregister(in.bytes(), opts);
         break;
      case 5:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous discover must be bytes");
         }
         out.discover_value = decode_discover(in.bytes(), opts);
         break;
      case 6:
         if (type != detail::wire_type::length_delimited) {
            exceptions::raise(exceptions::code::codec_error, "rendezvous discover response must be bytes");
         }
         out.discover_response_value = decode_discover_response(in.bytes(), opts);
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      exceptions::raise(exceptions::code::codec_error, "rendezvous message missing type");
   }
   return out;
}

std::vector<std::uint8_t> rendezvous::codec::make_cookie(std::uint64_t sequence) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(8);
   for (auto shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>((sequence >> shift) & 0xffU));
   }
   return out;
}

std::uint64_t rendezvous::codec::read_cookie(std::span<const std::uint8_t> cookie) {
   if (cookie.empty()) {
      return 0;
   }
   if (cookie.size() != 8) {
      exceptions::raise(exceptions::code::codec_error, "invalid rendezvous cookie");
   }
   auto out = std::uint64_t{};
   for (auto byte : cookie) {
      out = (out << 8U) | byte;
   }
   return out;
}

} // namespace fcl::p2p
