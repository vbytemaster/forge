module;

#include <string>

module fcl.transport.endpoint;

namespace fcl::transport {

std::string endpoint::authority() const {
   if (host_type == host_kind::ip6) {
      return "[" + host + "]:" + std::to_string(port);
   }
   return host + ":" + std::to_string(port);
}

} // namespace fcl::transport
