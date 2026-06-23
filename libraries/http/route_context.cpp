module;

#include <optional>
#include <string>
#include <string_view>

module forge.http.route_context;

namespace forge::http {

std::optional<std::string_view> route_context::route_param(std::string_view name) const {
   const auto iterator = route_params.find(std::string{name});
   if (iterator == route_params.end()) {
      return std::nullopt;
   }
   return iterator->second;
}

route_context make_route_context(const request& request) {
   return route_context{
       .request = request,
       .parsed_target = parse_target(std::string_view{request.target().data(), request.target().size()}),
   };
}

} // namespace forge::http
