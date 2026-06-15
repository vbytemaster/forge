module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

export module fcl.plugins.http_server.middleware;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.middleware;

export namespace fcl::plugins::http_server {

class middleware : public fcl::api::contract<middleware> {
 public:
   virtual ~middleware() = default;

   virtual void use(fcl::http::middleware_descriptor descriptor) = 0;
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::middleware, FCL_API_CONTRACT("fcl.plugins.http_server.middleware", 1, 0))
}
