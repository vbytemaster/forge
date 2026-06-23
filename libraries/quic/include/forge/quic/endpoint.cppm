module;

#include <cstdint>
#include <string>
#include <string_view>

export module forge.quic.endpoint;

import forge.quic.exceptions;

export namespace forge::quic {

struct endpoint {
   std::string host;
   std::uint16_t port = 0;

   [[nodiscard]] std::string authority() const;
};

[[nodiscard]] endpoint parse_endpoint(std::string_view value);

} // namespace forge::quic
