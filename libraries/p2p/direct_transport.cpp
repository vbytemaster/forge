module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
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
   profile* active_listener = nullptr;
};

registry::registry(fcl::asio::runtime& runtime, const node::options& options) : state_(std::make_unique<state>()) {
   register_quic_profile(*this, runtime, options);
   register_tcp_profile(*this, runtime, options);
}

registry::~registry() = default;

bool registry::listening() const noexcept {
   return state_ && state_->active_listener != nullptr && state_->active_listener->listening();
}

std::optional<fcl::p2p::endpoint> registry::local_endpoint() const {
   if (!listening()) {
      return std::nullopt;
   }
   return state_->active_listener->local_endpoint();
}

void registry::add(profile value) {
   if (!value.supports || !value.listening || !value.local_endpoint || !value.listen || !value.stop ||
       !value.async_connect || !value.async_accept) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P direct transport profile is empty");
   }
   state_->profiles.push_back(std::move(value));
}

void registry::listen(fcl::p2p::endpoint endpoint) {
   auto& selected = profile_for(state_->profiles, endpoint);
   selected.listen(std::move(endpoint));
   state_->active_listener = &selected;
}

void registry::stop() {
   if (state_ && state_->active_listener) {
      state_->active_listener->stop();
      state_->active_listener = nullptr;
   }
}

boost::asio::awaitable<connection> registry::async_connect(fcl::p2p::endpoint endpoint,
                                                           const node::connect_options& options) {
   auto& selected = profile_for(state_->profiles, endpoint);
   co_return co_await selected.async_connect(std::move(endpoint), options);
}

boost::asio::awaitable<connection> registry::async_accept() {
   if (!listening()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "P2P direct listener is not active");
   }
   co_return co_await state_->active_listener->async_accept();
}

} // namespace fcl::p2p::direct
