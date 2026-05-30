module;

#include <fcl/exception/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.p2p.hole_punch;

import fcl.multiformats.multiaddr;
import fcl.multiformats.types;
import fcl.multiformats.varint;
import fcl.multiformats.exceptions;
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

[[nodiscard]] hole_punch::message::message_kind checked_kind(std::uint64_t value) {
   if (value == static_cast<std::uint16_t>(hole_punch::message::message_kind::connect)) {
      return hole_punch::message::message_kind::connect;
   }
   if (value == static_cast<std::uint16_t>(hole_punch::message::message_kind::sync)) {
      return hole_punch::message::message_kind::sync;
   }
   FCL_THROW_EXCEPTION(exceptions::codec_error, "unknown DCUtR message type");
}

[[nodiscard]] std::vector<std::uint8_t> make_message_payload(const hole_punch::message& value) {
   auto out = std::vector<std::uint8_t>{};
   detail::append_uint64(out, 1, static_cast<std::uint16_t>(value.kind));
   for (const auto& endpoint : value.observed_endpoints) {
      const auto encoded = endpoint_bytes(endpoint);
      detail::append_bytes(out, 2, encoded);
   }
   return out;
}

[[nodiscard]] hole_punch::message read_message_payload(std::span<const std::uint8_t> bytes) {
   auto out = hole_punch::message{};
   auto in = detail::reader{bytes};
   auto saw_type = false;
   while (!in.done()) {
      const auto [field, type] = in.key();
      switch (field) {
      case 1:
         if (type != detail::wire_type::varint) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DCUtR type must be varint");
         }
         out.kind = checked_kind(in.read_varint());
         saw_type = true;
         break;
      case 2:
         if (type != detail::wire_type::length_delimited) {
            FCL_THROW_EXCEPTION(exceptions::codec_error, "DCUtR observed address must be bytes");
         }
         out.observed_endpoints.push_back(endpoint_from_bytes(in.bytes()));
         break;
      default:
         in.skip(type);
         break;
      }
   }
   if (!saw_type) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "DCUtR message is missing required type");
   }
   return out;
}

} // namespace

std::vector<std::uint8_t> hole_punch::codec::encode(const hole_punch::message& value) {
   return detail::wrap_message(make_message_payload(value));
}

hole_punch::message hole_punch::codec::decode(std::span<const std::uint8_t> bytes) {
   return decode(bytes, hole_punch::options{});
}

hole_punch::message hole_punch::codec::decode(std::span<const std::uint8_t> bytes, hole_punch::options options) {
   auto out = read_message_payload(detail::unwrap_message(bytes, options.max_message_size));
   if (out.observed_endpoints.size() > options.max_observed_endpoints) {
      FCL_THROW_EXCEPTION(exceptions::codec_error, "DCUtR message has too many observed addresses");
   }
   return out;
}

} // namespace fcl::p2p
