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
import fcl.plugins.http_server.exceptions;
import fcl.plugins.http_server.types;

#include "private/base_path.hxx"
#include "private/publication_store.hxx"

namespace fcl::plugins::http_server::detail {

void publication_store::publish(fcl::http::api_binding binding, publish_options options) {
   if (closed_) {
      FCL_THROW_EXCEPTION(exceptions::publication_closed, "HTTP server publication is already closed");
   }
   if (!options.base_path.empty()) {
      options.base_path = normalize_base_path(options.base_path, "base_path");
      if (options.base_path.empty()) {
         options.base_path = "/";
      }
   }
   publications_.push_back(publication_entry{.binding = std::move(binding), .options = std::move(options)});
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
   return publication_snapshot{.publications = publications_, .middleware = middleware_};
}

} // namespace fcl::plugins::http_server::detail
