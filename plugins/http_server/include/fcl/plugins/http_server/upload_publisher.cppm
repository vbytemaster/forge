module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <functional>
#include <string>

export module fcl.plugins.http_server.upload_publisher;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.route_context;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

struct upload_request {
   fcl::http::route_context& context;
   fcl::http::upload_reader upload;
};

using upload_handler = std::function<boost::asio::awaitable<fcl::http::stream_response>(upload_request&)>;

struct upload_publication {
   std::string route_path = "/upload";
   fcl::http::method method = fcl::http::method::post;
   fcl::http::upload_options options;
   upload_handler handler;
};

class upload_publisher : public fcl::api::contract<upload_publisher> {
 public:
   virtual ~upload_publisher() = default;

   virtual void publish(upload_publication publication, publish_options options = {}) = 0;
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::upload_publisher,
        FCL_API_CONTRACT("fcl.plugins.http_server.upload_publisher", 1, 0))
}
