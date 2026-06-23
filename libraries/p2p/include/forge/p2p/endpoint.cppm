module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module forge.p2p.endpoint;

import forge.multiformats.multiaddr;
import forge.p2p.identity;
import forge.transport.endpoint;

export namespace forge::p2p {

struct endpoint {
   using host_kind = forge::transport::endpoint::host_kind;
   using protocol_kind = forge::transport::endpoint::protocol_kind;

   enum class encapsulation_kind {
      none,
      ws,
      wss,
   };

   struct circuit {
      peer_id target;
   };

   forge::transport::endpoint transport;
   encapsulation_kind encapsulation = encapsulation_kind::none;
   std::optional<peer_id> peer;
   std::optional<circuit> relayed;

   [[nodiscard]] forge::multiformats::multiaddr to_multiaddr() const;
   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] bool is_direct_quic() const noexcept;
   [[nodiscard]] bool is_direct_tcp() const noexcept;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace forge::p2p
