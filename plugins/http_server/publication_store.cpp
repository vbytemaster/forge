module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.exceptions;
import fcl.http.api;
import fcl.http.middleware;
import fcl.plugins.http_server.file_publisher;
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;
import fcl.plugins.http_server.upload_publisher;

#include "private/base_path.hxx"
#include "private/publication_store.hxx"

namespace fcl::plugins::http_server::detail {
namespace {

publish_options normalize_options(publish_options options) {
   if (!options.base_path.empty()) {
      options.base_path = normalize_base_path(options.base_path, "base_path");
      if (options.base_path.empty()) {
         options.base_path = "/";
      }
   }
   return options;
}

void validate_route_path(std::string_view route_path) {
   if (route_path.empty() || route_path.front() != '/') {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server route path must start with /",
                          fcl::exceptions::ctx("field", "route_path"));
   }
}

} // namespace

void publication_store::publish(fcl::http::api_binding binding, publish_options options) {
   if (closed_) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is already closed");
   }
   options = normalize_options(std::move(options));
   publications_.push_back(publication_entry{.binding = std::move(binding), .options = std::move(options)});
}

void publication_store::publish(file_publication publication, publish_options options) {
   if (closed_) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server file publication is already closed");
   }
   validate_route_path(publication.route_path);
   if (publication.path_parameter.empty()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server file path parameter is empty");
   }
   files_.emplace_back(std::move(publication), normalize_options(std::move(options)));
}

void publication_store::publish(upload_publication publication, publish_options options) {
   if (closed_) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server upload publication is already closed");
   }
   validate_route_path(publication.route_path);
   if (!publication.handler) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server upload handler is empty");
   }
   uploads_.emplace_back(std::move(publication), normalize_options(std::move(options)));
}

void publication_store::use(fcl::http::middleware_descriptor descriptor) {
   if (closed_) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server middleware publication is already closed");
   }
   if (!descriptor.id.empty() &&
       std::ranges::any_of(middleware_, [&](const auto& value) { return value.id == descriptor.id; })) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_middleware, "duplicate HTTP server middleware id",
                          fcl::exceptions::ctx("middleware", descriptor.id));
   }
   middleware_.push_back(std::move(descriptor));
}

publication_snapshot publication_store::close() {
   closed_ = true;
   return publication_snapshot{.publications = publications_, .middleware = middleware_, .files = files_,
                               .uploads = uploads_};
}

} // namespace fcl::plugins::http_server::detail
