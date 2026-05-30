module;

#include <fcl/exception/macros.hpp>

#include <map>
#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.transport.registry;

import fcl.transport.exceptions;

namespace fcl::transport {
namespace {

void require_factory(bool valid) {
   if (!valid) {
      FCL_THROW_EXCEPTION(exceptions::protocol_error, "transport registry requires non-empty factories");
   }
}

} // namespace

struct registry::impl {
   struct stream_transport {
      stream_connector_factory connector;
      stream_listener_factory listener;
   };

   struct session_transport {
      session_connector_factory connector;
      session_listener_factory listener;
   };

   std::map<endpoint::protocol_kind, stream_transport> streams;
   std::map<endpoint::protocol_kind, session_transport> sessions;
};

registry::registry() : impl_(std::make_shared<impl>()) {}
registry::~registry() = default;
registry::registry(registry&&) noexcept = default;
registry& registry::operator=(registry&&) noexcept = default;

void registry::register_stream_transport(endpoint::protocol_kind protocol, stream_connector_factory connector,
                                         stream_listener_factory listener) {
   require_factory(static_cast<bool>(connector));
   require_factory(static_cast<bool>(listener));
   if (impl_->streams.contains(protocol)) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_registration, "transport stream protocol already registered");
   }
   impl_->streams.emplace(protocol, impl::stream_transport{.connector = std::move(connector), .listener = std::move(listener)});
}

void registry::register_session_transport(endpoint::protocol_kind protocol, session_connector_factory connector,
                                          session_listener_factory listener) {
   require_factory(static_cast<bool>(connector));
   require_factory(static_cast<bool>(listener));
   if (impl_->sessions.contains(protocol)) {
      FCL_THROW_EXCEPTION(exceptions::duplicate_registration, "transport session protocol already registered");
   }
   impl_->sessions.emplace(protocol,
                           impl::session_transport{.connector = std::move(connector), .listener = std::move(listener)});
}

bool registry::has_stream_transport(endpoint::protocol_kind protocol) const {
   return impl_->streams.contains(protocol);
}

bool registry::has_session_transport(endpoint::protocol_kind protocol) const {
   return impl_->sessions.contains(protocol);
}

boost::asio::awaitable<connected_stream> registry::async_connect_stream(endpoint remote, connect_options options) {
   const auto found = impl_->streams.find(remote.protocol);
   if (found == impl_->streams.end()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "no registered stream transport for endpoint protocol");
   }
   auto connector = found->second.connector();
   co_return co_await connector.async_connect(std::move(remote), options);
}

boost::asio::awaitable<connected_session> registry::async_connect_session(endpoint remote, connect_options options) {
   const auto found = impl_->sessions.find(remote.protocol);
   if (found == impl_->sessions.end()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "no registered session transport for endpoint protocol");
   }
   auto connector = found->second.connector();
   co_return co_await connector.async_connect(std::move(remote), options);
}

boost::asio::awaitable<stream_listener> registry::async_listen_stream(endpoint local, listen_options options) {
   const auto found = impl_->streams.find(local.protocol);
   if (found == impl_->streams.end()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "no registered stream listener for endpoint protocol");
   }
   co_return co_await found->second.listener(std::move(local), options);
}

boost::asio::awaitable<session_listener> registry::async_listen_session(endpoint local, listen_options options) {
   const auto found = impl_->sessions.find(local.protocol);
   if (found == impl_->sessions.end()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "no registered session listener for endpoint protocol");
   }
   co_return co_await found->second.listener(std::move(local), options);
}

} // namespace fcl::transport
