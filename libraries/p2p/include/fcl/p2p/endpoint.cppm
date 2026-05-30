module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module fcl.p2p.endpoint;

import fcl.multiformats.multiaddr;
import fcl.p2p.identity;
import fcl.transport.endpoint;

export namespace fcl::p2p {

struct endpoint {
   using host_kind = fcl::transport::endpoint::host_kind;
   using protocol_kind = fcl::transport::endpoint::protocol_kind;

   enum class encapsulation_kind {
      none,
      ws,
      wss,
   };

   struct circuit {
      peer_id target;
   };

   fcl::transport::endpoint transport;
   encapsulation_kind encapsulation = encapsulation_kind::none;
   std::optional<peer_id> peer;
   std::optional<circuit> relayed;

   [[nodiscard]] fcl::multiformats::multiaddr to_multiaddr() const;
   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] bool is_direct_quic() const noexcept;
   [[nodiscard]] bool is_direct_tcp() const noexcept;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace fcl::p2p
