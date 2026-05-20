module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module fcl.p2p.endpoint;

import fcl.p2p.identity;
import fcl.quic.endpoint;

export namespace fcl::p2p {

struct endpoint {
   enum class address_kind {
      ip4,
      ip6,
      dns,
      dns4,
      dns6,
   };

   address_kind kind = address_kind::ip4;
   std::string host;
   std::uint16_t port = 0;
   std::optional<peer_id> peer;

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] fcl::quic::endpoint quic_endpoint() const;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace fcl::p2p
