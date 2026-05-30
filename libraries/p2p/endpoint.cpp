module;

#include <fcl/exception/macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module fcl.p2p.endpoint;

import fcl.multiformats;
import fcl.p2p.exceptions;

namespace fcl::p2p {
namespace {

[[nodiscard]] fcl::multiformats::protocol_code address_kind_code(endpoint::address_kind kind) {
   using enum endpoint::address_kind;
   switch (kind) {
      case ip4:
         return fcl::multiformats::protocol_code::ip4;
      case ip6:
         return fcl::multiformats::protocol_code::ip6;
      case dns:
         return fcl::multiformats::protocol_code::dns;
      case dns4:
         return fcl::multiformats::protocol_code::dns4;
      case dns6:
         return fcl::multiformats::protocol_code::dns6;
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "unsupported P2P endpoint address kind");
}

[[nodiscard]] fcl::multiformats::protocol_code protocol_kind_code(endpoint::protocol_kind kind) {
   using enum endpoint::protocol_kind;
   switch (kind) {
      case quic_v1:
         return fcl::multiformats::protocol_code::quic_v1;
      case tcp:
         return fcl::multiformats::protocol_code::tcp;
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "unsupported P2P endpoint transport kind");
}

[[nodiscard]] endpoint::address_kind endpoint_kind(fcl::multiformats::protocol_code code) {
   switch (code) {
      case fcl::multiformats::protocol_code::ip4:
         return endpoint::address_kind::ip4;
      case fcl::multiformats::protocol_code::ip6:
         return endpoint::address_kind::ip6;
      case fcl::multiformats::protocol_code::dns:
         return endpoint::address_kind::dns;
      case fcl::multiformats::protocol_code::dns4:
         return endpoint::address_kind::dns4;
      case fcl::multiformats::protocol_code::dns6:
         return endpoint::address_kind::dns6;
      default:
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint must start with an address component");
   }
}

[[nodiscard]] endpoint::protocol_kind protocol_kind(fcl::multiformats::protocol_code code) {
   switch (code) {
      case fcl::multiformats::protocol_code::quic_v1:
         return endpoint::protocol_kind::quic_v1;
      case fcl::multiformats::protocol_code::tcp:
         return endpoint::protocol_kind::tcp;
      default:
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint is missing supported transport component");
   }
}

[[nodiscard]] fcl::multiformats::protocol_code encapsulation_code(endpoint::encapsulation_kind value) {
   switch (value) {
      case endpoint::encapsulation_kind::none:
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint has no encapsulation component");
      case endpoint::encapsulation_kind::ws:
         return fcl::multiformats::protocol_code::ws;
      case endpoint::encapsulation_kind::wss:
         return fcl::multiformats::protocol_code::wss;
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "unsupported P2P endpoint encapsulation kind");
}

} // namespace

fcl::multiformats::multiaddr endpoint::to_multiaddr() const {
   auto out = fcl::multiformats::multiaddr{};
   out.push({.code = address_kind_code(address.address), .value = address.host});
   if (address.protocol == protocol_kind::quic_v1) {
      out.push({.code = fcl::multiformats::protocol_code::udp, .value = std::to_string(address.port)});
      out.push({.code = fcl::multiformats::protocol_code::quic_v1, .value = {}});
   } else {
      out.push({.code = protocol_kind_code(address.protocol), .value = std::to_string(address.port)});
   }
   if (encapsulation != encapsulation_kind::none) {
      out.push({.code = encapsulation_code(encapsulation), .value = {}});
   }
   if (peer.has_value()) {
      out.push({.code = fcl::multiformats::protocol_code::p2p, .value = peer->to_string()});
   }
   if (relayed.has_value()) {
      out.push({.code = fcl::multiformats::protocol_code::p2p_circuit, .value = {}});
      out.push({.code = fcl::multiformats::protocol_code::p2p, .value = relayed->target.to_string()});
   }
   return out;
}

std::string endpoint::to_string() const {
   return to_multiaddr().to_string();
}

bool endpoint::is_direct_quic() const noexcept {
   return !relayed.has_value() && encapsulation == encapsulation_kind::none && address.protocol == protocol_kind::quic_v1;
}

bool endpoint::is_direct_tcp() const noexcept {
   return !relayed.has_value() && encapsulation == encapsulation_kind::none && address.protocol == protocol_kind::tcp;
}

endpoint parse_endpoint(std::string_view value) {
   const auto parsed = fcl::multiformats::multiaddr::parse(value);
   const auto& components = parsed.components();
   if (components.size() < 2) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint must include address and transport components");
   }

   auto result = endpoint{.address = fcl::transport::endpoint{
                              .address = endpoint_kind(components[0].code),
                              .host = components[0].value,
                          }};
   auto suffix = std::size_t{2};
   if (components[1].code == fcl::multiformats::protocol_code::udp) {
      if (components.size() < 3) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P UDP endpoint must include quic-v1 component");
      }
      try {
         result.address.port = static_cast<std::uint16_t>(std::stoul(components[1].value));
      } catch (...) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint port is invalid");
      }
      if (components[2].code != fcl::multiformats::protocol_code::quic_v1) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P UDP endpoint is missing quic-v1 component");
      }
      result.address.protocol = endpoint::protocol_kind::quic_v1;
      suffix = 3;
   } else {
      result.address.protocol = protocol_kind(components[1].code);
      try {
         result.address.port = static_cast<std::uint16_t>(std::stoul(components[1].value));
      } catch (...) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint port is invalid");
      }
   }

   if (suffix < components.size() && (components[suffix].code == fcl::multiformats::protocol_code::ws ||
                                      components[suffix].code == fcl::multiformats::protocol_code::wss)) {
      result.encapsulation = components[suffix].code == fcl::multiformats::protocol_code::ws
                                 ? endpoint::encapsulation_kind::ws
                                 : endpoint::encapsulation_kind::wss;
      ++suffix;
   }

   if (suffix < components.size() && components[suffix].code == fcl::multiformats::protocol_code::p2p) {
      result.peer = peer_id::from_string(components[suffix].value);
      ++suffix;
   }

   if (suffix < components.size() && components[suffix].code == fcl::multiformats::protocol_code::p2p_circuit) {
      ++suffix;
      if (suffix >= components.size() || components[suffix].code != fcl::multiformats::protocol_code::p2p) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P relayed endpoint must use p2p-circuit/p2p suffix");
      }
      result.relayed = endpoint::circuit{.target = peer_id::from_string(components[suffix].value)};
      ++suffix;
   }

   if (suffix != components.size()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint contains unsupported extra components");
   }
   return result;
}

} // namespace fcl::p2p
