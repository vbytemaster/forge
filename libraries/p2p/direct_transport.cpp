module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.node;

import fcl.asio.runtime;
import fcl.p2p.endpoint;
import fcl.p2p.exceptions;
import fcl.transport.session;

#include "direct_transport.hpp"

namespace fcl::p2p::direct {
namespace {

[[nodiscard]] profile& profile_for(std::vector<profile>& profiles, const fcl::p2p::endpoint& endpoint) {
   for (auto& candidate : profiles) {
      if (candidate.supports(endpoint)) {
         return candidate;
      }
   }
   FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "unsupported P2P direct transport");
}

} // namespace

struct registry::state {
   std::vector<profile> profiles;
};

registry::registry(fcl::asio::runtime& runtime, const node::options& options) : state_(std::make_unique<state>()) {
   register_quic_profile(*this, runtime, options);
   register_tcp_profile(*this, runtime, options);
}

registry::~registry() = default;

bool registry::listening() const noexcept {
   return state_ && std::ranges::any_of(state_->profiles, [](const profile& value) {
      return value.listening();
   });
}

std::optional<fcl::p2p::endpoint> registry::local_endpoint() const {
   auto endpoints = local_endpoints();
   if (endpoints.empty()) {
      return std::nullopt;
   }
   return endpoints.front();
}

std::vector<fcl::p2p::endpoint> registry::local_endpoints() const {
   auto out = std::vector<fcl::p2p::endpoint>{};
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
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P direct transport profile is empty");
   }
   state_->profiles.push_back(std::move(value));
}

fcl::p2p::endpoint registry::listen(fcl::p2p::endpoint endpoint) {
   const auto requested = endpoint.to_string();
   const auto existing = local_endpoints();
   if (std::ranges::any_of(existing, [&](const auto& value) { return value.to_string() == requested; })) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P direct listener endpoint is already active");
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

boost::asio::awaitable<connection> registry::async_connect(fcl::p2p::endpoint endpoint,
                                                           const node::connect_options& options) {
   auto& selected = profile_for(state_->profiles, endpoint);
   co_return co_await selected.async_connect(std::move(endpoint), options);
}

boost::asio::awaitable<connection> registry::async_accept(fcl::p2p::endpoint endpoint) {
   auto& selected = profile_for(state_->profiles, endpoint);
   co_return co_await selected.async_accept(std::move(endpoint));
}

} // namespace fcl::p2p::direct
