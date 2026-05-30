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
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "unsupported P2P endpoint address kind");
}

[[nodiscard]] fcl::multiformats::multicodec_code protocol_kind_code(endpoint::protocol_kind kind) {
   using enum endpoint::protocol_kind;
   switch (kind) {
      case quic_v1:
         return fcl::multiformats::multicodec_code::quic_v1;
      case tcp:
         return fcl::multiformats::multicodec_code::tcp;
   }
   FCL_THROW_EXCEPTION(exceptions::invalid_options, "unsupported P2P endpoint transport kind");
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
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint must start with an address component");
   }
}

[[nodiscard]] endpoint::protocol_kind protocol_kind(fcl::multiformats::multicodec_code code) {
   switch (code) {
      case fcl::multiformats::multicodec_code::quic_v1:
         return endpoint::protocol_kind::quic_v1;
      case fcl::multiformats::multicodec_code::tcp:
         return endpoint::protocol_kind::tcp;
      default:
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint is missing supported transport component");
   }
}

} // namespace

std::string endpoint::to_string() const {
   auto out = fcl::multiformats::address{};
   out.push({.code = address_kind_code(address.address), .value = address.host});
   if (address.protocol == protocol_kind::quic_v1) {
      out.push({.code = fcl::multiformats::multicodec_code::udp, .value = std::to_string(address.port)});
      out.push({.code = fcl::multiformats::multicodec_code::quic_v1, .value = {}});
   } else {
      out.push({.code = protocol_kind_code(address.protocol), .value = std::to_string(address.port)});
   }
   if (peer.has_value()) {
      out.push({.code = fcl::multiformats::multicodec_code::p2p, .value = peer->to_string()});
   }
   if (relayed.has_value()) {
      if (!peer.has_value()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P relayed endpoint requires relay peer");
      }
      out.push({.code = fcl::multiformats::multicodec_code::p2p_circuit, .value = {}});
      out.push({.code = fcl::multiformats::multicodec_code::p2p, .value = relayed->target.to_string()});
   }
   return out.to_string();
}

bool endpoint::is_direct_quic() const noexcept {
   return !relayed.has_value() && address.protocol == protocol_kind::quic_v1;
}

bool endpoint::is_direct_tcp() const noexcept {
   return !relayed.has_value() && address.protocol == protocol_kind::tcp;
}

endpoint parse_endpoint(std::string_view value) {
   const auto address = fcl::multiformats::address::parse(value);
   const auto& components = address.components();
   if (components.size() < 2) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint must include address and transport components");
   }

   auto result = endpoint{.address = fcl::transport::endpoint{
                              .address = endpoint_kind(components[0].code),
                              .host = components[0].value,
                          }};
   auto suffix = std::size_t{2};
   if (components[1].code == fcl::multiformats::multicodec_code::udp) {
      if (components.size() < 3) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P UDP endpoint must include quic-v1 component");
      }
      try {
         result.address.port = static_cast<std::uint16_t>(std::stoul(components[1].value));
      } catch (...) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint port is invalid");
      }
      if (components[2].code != fcl::multiformats::multicodec_code::quic_v1) {
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
   const auto remaining = components.size() - suffix;
   if (remaining == 1 || remaining == 3) {
      if (components[suffix].code != fcl::multiformats::multicodec_code::p2p) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "unexpected component after transport endpoint");
      }
      result.peer = peer_id::from_string(components[suffix].value);
      if (remaining == 3) {
         if (components[suffix + 1].code != fcl::multiformats::multicodec_code::p2p_circuit ||
             components[suffix + 2].code != fcl::multiformats::multicodec_code::p2p) {
            FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P relayed endpoint must use p2p-circuit/p2p suffix");
         }
         result.relayed = endpoint::circuit{.target = peer_id::from_string(components[suffix + 2].value)};
      }
   } else if (remaining != 0) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P endpoint contains unsupported extra components");
   }
   return result;
}

} // namespace fcl::p2p
