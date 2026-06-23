module;

#include <cstdint>
#include <optional>
#include <string>
#include <span>
#include <vector>

export module forge.p2p.identify;

import forge.multiformats.types;
import forge.p2p.endpoint;
import forge.p2p.protocol;

export namespace forge::p2p {

namespace identify {
   struct document {
      std::string protocol_version;
      std::string agent_version;
      std::vector<std::uint8_t> public_key;
      std::vector<endpoint> listen_endpoints;
      std::optional<endpoint> observed_endpoint;
      std::vector<protocol_id> protocols;
      std::vector<std::uint8_t> signed_peer_record;
   };

   [[nodiscard]] forge::multiformats::bytes encode(const document& value);
   [[nodiscard]] document decode(std::span<const std::uint8_t> bytes);
} // namespace identify

} // namespace forge::p2p
