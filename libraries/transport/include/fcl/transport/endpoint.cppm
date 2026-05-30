module;

#include <cstdint>
#include <string>

export module fcl.transport.endpoint;

export namespace fcl::transport {

struct endpoint {
   enum class address_kind {
      ip4,
      ip6,
      dns,
      dns4,
      dns6,
   };

   enum class protocol_kind {
      quic_v1,
      tcp,
   };

   address_kind address = address_kind::ip4;
   protocol_kind protocol = protocol_kind::quic_v1;
   std::string host;
   std::uint16_t port = 0;

   [[nodiscard]] std::string authority() const;
};

} // namespace fcl::transport
