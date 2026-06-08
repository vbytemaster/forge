module;

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

export module fcl.http.route_context;

import fcl.asio.runtime;
import fcl.http.target;
import fcl.http.types;

export namespace fcl::http {

struct route_context {
   const request& request;
   target parsed_target;
   std::unordered_map<std::string, std::string> route_params;
   fcl::asio::runtime* runtime = nullptr;

   [[nodiscard]] std::optional<std::string_view> route_param(std::string_view name) const;
};

route_context make_route_context(const request& request);

} // namespace fcl::http
