module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module fcl.p2p.endpoint;

import fcl.multiformats;
import fcl.p2p.errors;

namespace fcl::p2p {
namespace {

[[nodiscard]] fcl::multiformats::multicodec_code address_kind_code(endpoint::address_kind kind) {
   using enum endpoint::address_kind;
   switch (kind) {
      case ip4:
         return fcl::multiformats::multicodec_code::ip4;
      case ip6:
         return fcl::multiformats::multicodec_code::ip6;
      case dns:
         return fcl::multiformats::multicodec_code::dns;
      case dns4:
         return fcl::multiformats::multicodec_code::dns4;
      case dns6:
         return fcl::multiformats::multicodec_code::dns6;
   }
   throw_p2p_error(error_kind::invalid_options, "unsupported P2P endpoint address kind");
}

[[nodiscard]] endpoint::address_kind endpoint_kind(fcl::multiformats::multicodec_code code) {
   switch (code) {
      case fcl::multiformats::multicodec_code::ip4:
         return endpoint::address_kind::ip4;
      case fcl::multiformats::multicodec_code::ip6:
         return endpoint::address_kind::ip6;
      case fcl::multiformats::multicodec_code::dns:
         return endpoint::address_kind::dns;
      case fcl::multiformats::multicodec_code::dns4:
         return endpoint::address_kind::dns4;
      case fcl::multiformats::multicodec_code::dns6:
         return endpoint::address_kind::dns6;
      default:
         throw_p2p_error(error_kind::invalid_options, "P2P endpoint must start with an address component");
   }
}

} // namespace

std::string endpoint::to_string() const {
   auto address = fcl::multiformats::address{};
   address.push({.code = address_kind_code(kind), .value = host});
   address.push({.code = fcl::multiformats::multicodec_code::udp, .value = std::to_string(port)});
   address.push({.code = fcl::multiformats::multicodec_code::quic_v1, .value = {}});
   if (peer.has_value()) {
      address.push({.code = fcl::multiformats::multicodec_code::p2p, .value = peer->to_string()});
   }
   return address.to_string();
}

fcl::quic::endpoint endpoint::quic_endpoint() const {
   return {.host = host, .port = port};
}

endpoint parse_endpoint(std::string_view value) {
   const auto address = fcl::multiformats::address::parse(value);
   const auto& components = address.components();
   if (components.size() < 3) {
      throw_p2p_error(error_kind::invalid_options, "P2P endpoint must include address/udp/quic-v1 components");
   }

   auto result = endpoint{.kind = endpoint_kind(components[0].code), .host = components[0].value};
   if (components[1].code != fcl::multiformats::multicodec_code::udp) {
      throw_p2p_error(error_kind::invalid_options, "P2P libp2p QUIC endpoint must use udp before quic-v1");
   }
   try {
      result.port = static_cast<std::uint16_t>(std::stoul(components[1].value));
   } catch (...) {
      throw_p2p_error(error_kind::invalid_options, "P2P endpoint UDP port is invalid");
   }
   if (components[2].code != fcl::multiformats::multicodec_code::quic_v1) {
      throw_p2p_error(error_kind::invalid_options, "P2P endpoint is missing quic-v1 component");
   }
   if (components.size() == 4) {
      if (components[3].code != fcl::multiformats::multicodec_code::p2p) {
         throw_p2p_error(error_kind::invalid_options, "unexpected component after quic-v1");
      }
      result.peer = peer_id::from_string(components[3].value);
   } else if (components.size() > 4) {
      throw_p2p_error(error_kind::invalid_options, "P2P endpoint contains unsupported extra components");
   }
   return result;
}

} // namespace fcl::p2p
