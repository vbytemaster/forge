module;

#include <functional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.http.middleware;

import fcl.http.route_context;
import fcl.http.types;

export namespace fcl::http {

using route_handler = std::function<boost::asio::awaitable<response>(route_context&)>;
using next_handler = std::function<boost::asio::awaitable<response>()>;
using middleware = std::function<boost::asio::awaitable<response>(route_context&, next_handler)>;
using middleware_list = std::vector<middleware>;

enum class middleware_phase {
   request_context = 1,
   security = 2,
   limits = 3,
   before_handler = 4,
   after_handler = 5,
   error = 6,
};

struct middleware_descriptor {
   std::string id;
   middleware_phase phase = middleware_phase::before_handler;
   int order = 0;
   std::string path_prefix = "/";
   middleware handler;
};

boost::asio::awaitable<response> run_middleware_chain(middleware_list middlewares, route_context& context,
                                                      route_handler terminal);

} // namespace fcl::http
