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

#include "details/state.hxx"
#include "details/api_facade.hxx"

namespace fcl::plugins::http_server {

plugin::api_impl::api_impl(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

boost::asio::awaitable<void> plugin::api_impl::publish(publication value) {
   impl_->add(std::move(value));
   co_return;
}

boost::asio::awaitable<void> plugin::api_impl::publish(fcl::http::api_binding binding, publish_options options) {
   auto shared = std::make_shared<fcl::http::api_binding>(std::move(binding));
   impl_->add(publication{
      .build = [shared](const fcl::api::registry&) {
         return *shared;
      },
      .options = std::move(options),
   });
   co_return;
}

boost::asio::awaitable<void> plugin::api_impl::use(fcl::http::middleware_descriptor descriptor) {
   impl_->add(std::move(descriptor));
   co_return;
}

} // namespace fcl::plugins::http_server
