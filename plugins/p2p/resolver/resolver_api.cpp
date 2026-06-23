module;

#include <forge/exceptions/macros.hpp>

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

module forge.plugins.p2p.resolver.plugin;

import forge.api.descriptor;
import forge.transport.api.connection;
import forge.exceptions;
import forge.p2p.identity;
import forge.p2p.protocol;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.types;

#include "details/descriptor_projection.hxx"
#include "details/plugin_impl.hxx"
#include "details/resolver_api.hxx"

namespace forge::plugins::p2p::resolver {

plugin::resolver_api::resolver_api(std::shared_ptr<plugin::impl> impl) : impl_{std::move(impl)} {}

void plugin::resolver_api::publish_api(forge::api::binding_plan plan,
                                   forge::p2p::protocol_id protocol,
                                   publish_options options) {
   impl_->add_local(std::move(plan), std::move(protocol), std::move(options));
}

std::vector<entry> plugin::resolver_api::local_apis() const {
   (void)impl_->require_p2p();
   return impl_->local_snapshot();
}

boost::asio::awaitable<std::vector<entry>>
plugin::resolver_api::peer_apis(forge::p2p::peer_id peer, resolve_options options) {
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
plugin::resolver_api::resolve(forge::p2p::peer_id peer, forge::api::api_ref api, resolve_options options) {
   const auto entries = co_await peer_apis(std::move(peer), options);
   if (auto selected = select_compatible(entries, api)) {
      co_return resolution{.api = std::move(*selected)};
   }
   const auto has_same_api = std::ranges::any_of(entries, [&](const auto& candidate) {
      return candidate.id == api.id && candidate.version.major == api.major;
   });
   if (has_same_api) {
      FORGE_THROW_EXCEPTION(exceptions::incompatible_api, "remote peer has incompatible API revision",
                          forge::exceptions::ctx("api", api.id.value));
   }
   FORGE_THROW_EXCEPTION(exceptions::not_found, "remote peer does not advertise requested API",
                       forge::exceptions::ctx("api", api.id.value));
}

boost::asio::awaitable<resolved_connection>
plugin::resolver_api::open_resolved_connection(forge::p2p::peer_id peer,
                                           forge::api::api_ref api,
                                           forge::api::descriptor descriptor,
                                           resolve_options options) {
   auto selected = co_await resolve(peer, api, options);
   validate_descriptor_compatible(descriptor, selected.api);
   auto protocol = forge::p2p::protocol_id{.value = selected.api.protocol};
   auto connection = co_await impl_->p2p->open_api_connection(
      std::move(peer), std::move(protocol),
      forge::plugins::p2p::node::remote_options{
         .open_deadline = impl_->open_deadline(options),
         .codec = selected.api.codec,
         .max_inflight = static_cast<std::size_t>(selected.api.max_inflight),
         .max_frame_size = static_cast<std::uint32_t>(selected.api.max_frame_size),
      });
   co_return resolved_connection{
      .connection = std::move(connection),
      .selected = forge::api::api_ref{
         .id = std::move(selected.api.id),
         .major = selected.api.version.major,
         .min_revision = selected.api.version.revision,
      },
   };
}

} // namespace forge::plugins::p2p::resolver
