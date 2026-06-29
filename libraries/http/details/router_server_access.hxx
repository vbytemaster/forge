#pragma once

namespace forge::http {

class response;
class route_context;
class router;

namespace detail {

struct router_server_access {
   static bool reject_without_body(const router& router_value, route_context& context, response& output);
};

} // namespace detail
} // namespace forge::http
