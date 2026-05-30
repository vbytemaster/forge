module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module fcl.p2p.endpoint;

import fcl.p2p.identity;
import fcl.transport.endpoint;

export namespace fcl::p2p {

struct endpoint {
   using address_kind = fcl::transport::endpoint::address_kind;
   using protocol_kind = fcl::transport::endpoint::protocol_kind;

   struct circuit {
      peer_id target;
   };

   fcl::transport::endpoint address;
   std::optional<peer_id> peer;
   std::optional<circuit> relayed;

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] bool is_direct_quic() const noexcept;
   [[nodiscard]] bool is_direct_tcp() const noexcept;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace fcl::p2p
