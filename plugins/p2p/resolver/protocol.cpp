module;

#include <fcl/api/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p.resolver.plugin;

import fcl.api.binding;
import fcl.api.registry;
import fcl.transport.api.connection;
import fcl.transport.api.options;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.plugins.p2p.resolver.api;
import fcl.plugins.p2p.resolver.types;
import fcl.plugins.p2p.node.api;
import fcl.plugins.p2p.node.types;

#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::p2p::resolver::detail {

class resolver_protocol
    : public fcl::api::contract<resolver_protocol, fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~resolver_protocol() = default;
   virtual boost::asio::awaitable<response> query(::fcl::plugins::p2p::resolver::query request) = 0;
};

} // namespace fcl::plugins::p2p::resolver::detail

FCL_API(::fcl::plugins::p2p::resolver::detail::resolver_protocol,
        FCL_API_CONTRACT("fcl.plugins.p2p.resolver.protocol", 1, 0), FCL_API_METHOD(query))

namespace fcl::plugins::p2p::resolver {

class plugin::resolver_protocol_service final : public detail::resolver_protocol {
 public:
   explicit resolver_protocol_service(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

   boost::asio::awaitable<response> query(::fcl::plugins::p2p::resolver::query request) override {
      co_return impl_->query_local(request);
   }

 private:
   std::shared_ptr<plugin::impl> impl_;
};

void plugin::impl::install_protocol() {
   protocol_registry.clear();
   protocol_registry.install<detail::resolver_protocol>(
      std::make_shared<plugin::resolver_protocol_service>(shared_from_this()));
   auto plan = fcl::api::binding()
                  .serve(protocol_registry)
                  .export_api<detail::resolver_protocol>({.id = {resolver_api_id}, .major = 1, .min_revision = 0})
                  .build();
   p2p->publish_api(std::move(plan), protocol, resolver_transport);
}

boost::asio::awaitable<std::vector<entry>>
plugin::impl::query_remote_apis(fcl::p2p::peer_id peer, resolve_options options) {
   auto remote = co_await p2p->remote<detail::resolver_protocol>(
      peer, protocol,
      fcl::plugins::p2p::node::remote_options{.open_deadline = open_deadline(options),
                                             .deadline = query_deadline(options)});
   auto result = co_await remote->query(query{});
   co_return std::move(result.apis);
}

} // namespace fcl::plugins::p2p::resolver
