module;

#include <cstdint>
#include <string>
#include <string_view>

export module fcl.quic.endpoint;

import fcl.quic.exceptions;

export namespace fcl::quic {

struct endpoint {
   std::string host;
   std::uint16_t port = 0;

   [[nodiscard]] std::string authority() const;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace fcl::quic
