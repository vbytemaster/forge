module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_api_resolver.plugin;

import fcl.api.descriptor;
import fcl.api.transport.connection;
import fcl.exceptions;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.plugins.p2p_api_resolver.api;
import fcl.plugins.p2p_api_resolver.exceptions;
import fcl.plugins.p2p_api_resolver.types;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_node.types;

#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"
#include "details/resolver_api.hxx"

namespace fcl::plugins::p2p_api_resolver {

plugin::resolver_api::resolver_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

void plugin::resolver_api::publish_api(fcl::api::binding_plan plan,
                                   fcl::p2p::protocol_id protocol,
                                   publish_options options) {
   impl_->add_local(std::move(plan), std::move(protocol), std::move(options));
}

std::vector<entry> plugin::resolver_api::local_apis() const {
   (void)impl_->require_p2p();
   return impl_->local_snapshot();
}

boost::asio::awaitable<std::vector<entry>>
plugin::resolver_api::peer_apis(fcl::p2p::peer_id peer, resolve_options options) {
   (void)impl_->require_p2p();
   if (auto cached = impl_->cached_peer(peer, options)) {
      co_return *cached;
   }

   auto entries = co_await impl_->query_remote_apis(peer, options);
   validate_response(entries, impl_->settings);
   impl_->store_peer(peer, entries);
   co_return entries;
}

boost::asio::awaitable<resolution>
plugin::resolver_api::resolve(fcl::p2p::peer_id peer, fcl::api::api_ref api, resolve_options options) {
   const auto entries = co_await peer_apis(std::move(peer), options);
   if (auto selected = select_compatible(entries, api)) {
      co_return resolution{.api = std::move(*selected)};
   }
   const auto has_same_api = std::ranges::any_of(entries, [&](const auto& candidate) {
      return candidate.id == api.id && candidate.version.major == api.major;
   });
   if (has_same_api) {
      FCL_THROW_EXCEPTION(exceptions::incompatible_api, "remote peer has incompatible API revision",
                          fcl::exceptions::ctx("api", api.id.value));
   }
   FCL_THROW_EXCEPTION(exceptions::not_found, "remote peer does not advertise requested API",
                       fcl::exceptions::ctx("api", api.id.value));
}

boost::asio::awaitable<resolved_connection>
plugin::resolver_api::open_resolved_connection(fcl::p2p::peer_id peer,
                                           fcl::api::api_ref api,
                                           fcl::api::descriptor descriptor,
                                           resolve_options options) {
   auto selected = co_await resolve(peer, api, options);
   validate_descriptor_compatible(descriptor, selected.api);
   auto protocol = fcl::p2p::protocol_id{.value = selected.api.protocol};
   auto connection = co_await impl_->p2p->open_api_connection(
      std::move(peer), std::move(protocol),
      fcl::plugins::p2p_node::remote_options{
         .open_deadline = impl_->open_deadline(options),
         .codec = selected.api.codec,
         .max_inflight = static_cast<std::size_t>(selected.api.max_inflight),
         .max_frame_size = static_cast<std::uint32_t>(selected.api.max_frame_size),
      });
   co_return resolved_connection{
      .connection = std::move(connection),
      .selected = fcl::api::api_ref{
         .id = std::move(selected.api.id),
         .major = selected.api.version.major,
         .min_revision = selected.api.version.revision,
      },
   };
}

} // namespace fcl::plugins::p2p_api_resolver
