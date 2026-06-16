module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.api.registry;
import fcl.asio.runtime;
import fcl.http.api;
import fcl.http.middleware;
import fcl.http.server;
import fcl.plugins.http_server.api;
import fcl.plugins.http_server.types;

#include "details/plugin_impl.hxx"
#include "details/publication_api.hxx"

namespace fcl::plugins::http_server {

plugin::publication_api::publication_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

boost::asio::awaitable<void> plugin::publication_api::publish(publication value) {
   impl_->add(std::move(value));
   co_return;
}

boost::asio::awaitable<void> plugin::publication_api::use(fcl::http::middleware_descriptor descriptor) {
   impl_->add(std::move(descriptor));
   co_return;
}

} // namespace fcl::plugins::http_server
