module;

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

export module forge.http.route_context;

import forge.asio.runtime;
import forge.http.target;
import forge.http.types;

export namespace forge::http {

struct route_context {
   const request& request;
   target parsed_target;
   std::unordered_map<std::string, std::string> route_params;
   forge::asio::runtime* runtime = nullptr;

   [[nodiscard]] std::optional<std::string_view> route_param(std::string_view name) const;
};

route_context make_route_context(const request& request);

} // namespace forge::http
