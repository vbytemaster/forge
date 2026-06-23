module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.p2p.node;

import forge.asio.runtime;
import forge.p2p.endpoint;
import forge.p2p.exceptions;
import forge.transport.session;

#include "direct_transport.hpp"

namespace forge::p2p::direct {
namespace {

[[nodiscard]] profile& profile_for(std::vector<profile>& profiles, const forge::p2p::endpoint& endpoint) {
   for (auto& candidate : profiles) {
      if (candidate.supports(endpoint)) {
         return candidate;
      }
   }
   FORGE_THROW_EXCEPTION(exceptions::unsupported_protocol, "unsupported P2P direct transport");
}

} // namespace

struct registry::state {
   std::vector<profile> profiles;
};

registry::registry(forge::asio::runtime& runtime, const node::options& options) : state_(std::make_unique<state>()) {
   register_quic_profile(*this, runtime, options);
   register_tcp_profile(*this, runtime, options);
}

registry::~registry() = default;

bool registry::listening() const noexcept {
   return state_ && std::ranges::any_of(state_->profiles, [](const profile& value) {
      return value.listening();
   });
}

std::optional<forge::p2p::endpoint> registry::local_endpoint() const {
   auto endpoints = local_endpoints();
   if (endpoints.empty()) {
      return std::nullopt;
   }
   return endpoints.front();
}

std::vector<forge::p2p::endpoint> registry::local_endpoints() const {
   auto out = std::vector<forge::p2p::endpoint>{};
   if (!state_) {
      return out;
   }
   for (const auto& value : state_->profiles) {
      auto endpoints = value.local_endpoints();
      out.insert(out.end(), std::make_move_iterator(endpoints.begin()), std::make_move_iterator(endpoints.end()));
   }
   return out;
}

void registry::add(profile value) {
   if (!value.supports || !value.listening || !value.local_endpoints || !value.listen || !value.stop ||
       !value.async_connect || !value.async_accept) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P direct transport profile is empty");
   }
   state_->profiles.push_back(std::move(value));
}

forge::p2p::endpoint registry::listen(forge::p2p::endpoint endpoint) {
   const auto requested = endpoint.to_string();
   const auto existing = local_endpoints();
   if (std::ranges::any_of(existing, [&](const auto& value) { return value.to_string() == requested; })) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "P2P direct listener endpoint is already active");
   }
   auto& selected = profile_for(state_->profiles, endpoint);
   return selected.listen(std::move(endpoint));
}

void registry::stop() {
   if (!state_) {
      return;
   }
   for (auto& value : state_->profiles) {
      if (value.listening()) {
         value.stop();
      }
   }
}

boost::asio::awaitable<connection> registry::async_connect(forge::p2p::endpoint endpoint,
                                                           const node::connect_options& options) {
   auto& selected = profile_for(state_->profiles, endpoint);
   co_return co_await selected.async_connect(std::move(endpoint), options);
}

boost::asio::awaitable<connection> registry::async_accept(forge::p2p::endpoint endpoint) {
   auto& selected = profile_for(state_->profiles, endpoint);
   co_return co_await selected.async_accept(std::move(endpoint));
}

} // namespace forge::p2p::direct
