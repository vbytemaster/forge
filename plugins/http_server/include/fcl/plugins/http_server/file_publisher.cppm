module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <filesystem>
#include <string>

export module fcl.plugins.http_server.file_publisher;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.file;
import fcl.plugins.http_server.types;

export namespace fcl::plugins::http_server {

struct file_publication {
   std::filesystem::path root;
   std::string route_path = "/files/:path";
   std::string path_parameter = "path";
   fcl::http::file_options options;
};

class file_publisher : public fcl::api::contract<file_publisher> {
 public:
   virtual ~file_publisher() = default;

   virtual void publish(file_publication publication, publish_options options = {}) = 0;
};

} // namespace fcl::plugins::http_server

export {
FCL_API(::fcl::plugins::http_server::file_publisher,
        FCL_API_CONTRACT("fcl.plugins.http_server.file_publisher", 1, 0))
}
