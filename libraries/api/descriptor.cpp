module;

#include <string>
#include <string_view>
#include <utility>

module fcl.api.descriptor;

namespace fcl::api {

bool compatible(const descriptor& available, const api_ref& requested) noexcept {
   return available.id == requested.id && available.version.major == requested.major &&
          available.version.revision >= requested.min_revision;
}

const method_descriptor* find_method(const descriptor& api, std::string_view name) noexcept {
   for (const auto& method : api.methods) {
      if (method.name == name) {
         return &method;
      }
   }
   return nullptr;
}

api_error::api_error(std::string message) : std::runtime_error(std::move(message)) {}

} // namespace fcl::api
