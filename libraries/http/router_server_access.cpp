#include "details/router_server_access.hxx"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

import forge.http.route_context;
import forge.http.router;
import forge.http.target;
import forge.http.types;

#include "details/router_match.hxx"

namespace forge::http {

bool detail::router_server_access::reject_without_body(const router& router_value, route_context& context,
                                                       response& output) {
   for (const auto& route : router_value.routes_) {
      if (route.verb == context.request.method() && detail::match_path(route, context.parsed_target, nullptr)) {
         return false;
      }
   }
   for (const auto& route : router_value.stream_routes_) {
      if (route.verb == context.request.method() && detail::match_path(route, context.parsed_target, nullptr)) {
         return false;
      }
   }

   if (detail::path_exists(router_value.routes_, context.parsed_target) ||
       detail::path_exists(router_value.stream_routes_, context.parsed_target)) {
      output = make_text_response(context.request, status::method_not_allowed, "method not allowed");
      return true;
   }
   if (detail::path_exists(router_value.websocket_routes_, context.parsed_target)) {
      output = make_text_response(context.request, status::upgrade_required, "websocket upgrade required");
      return true;
   }
   output = make_text_response(context.request, status::not_found, "not found");
   return true;
}

} // namespace forge::http
