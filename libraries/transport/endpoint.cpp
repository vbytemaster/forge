module;

#include <string>

module fcl.transport.endpoint;

namespace fcl::transport {

std::string endpoint::authority() const {
   return host + ":" + std::to_string(port);
}

} // namespace fcl::transport
