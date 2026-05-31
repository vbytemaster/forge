module;

#include <fcl/exception/macros.hpp>

#include <memory>
#include <optional>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.node;

import fcl.asio.runtime;
import fcl.p2p.endpoint;
import fcl.p2p.exceptions;
import fcl.p2p.stream;
import fcl.tcp.connection;
import fcl.tcp.connector;
import fcl.tcp.exceptions;
import fcl.tcp.listener;
import fcl.transport.connector;
import fcl.transport.session;
import fcl.transport.stream;
import fcl.yamux.session;

#include "direct_transport.hpp"
#include "stream_upgrade.hpp"

namespace fcl::p2p::direct {
namespace {

[[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(fcl::transport::endpoint value) {
   return fcl::p2p::endpoint{.transport = std::move(value)};
}

[[nodiscard]] exceptions::code map_tcp_error(fcl::tcp::exceptions::code kind) noexcept {
   using tcp_kind = fcl::tcp::exceptions::code;
   switch (kind) {
   case tcp_kind::invalid_endpoint:
   case tcp_kind::invalid_options:
      return exceptions::code::invalid_options;
   case tcp_kind::canceled:
      return exceptions::code::canceled;
   case tcp_kind::closed:
      return exceptions::code::closed;
   case tcp_kind::connect_failed:
   case tcp_kind::listen_failed:
   case tcp_kind::accept_failed:
   case tcp_kind::io_error:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_tcp_as_p2p(const fcl::exception::base& error) {
   const auto code = fcl::tcp::exceptions::code_of(error);
   if (code) {
      FCL_THROW_CODE(map_tcp_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] std::optional<peer_id> expected_peer_for(const fcl::p2p::endpoint& endpoint,
                                                       const node::connect_options& options) {
   if (options.expected_peer) {
      return options.expected_peer;
   }
   return endpoint.peer;
}

class tcp_profile final {
 public:
   tcp_profile(fcl::asio::runtime& runtime_value, const node::options& options_value)
       : runtime_(runtime_value), options_(options_value),
         connector_(runtime_value.context().get_executor()) {}

   [[nodiscard]] bool supports(const fcl::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_tcp();
   }

   [[nodiscard]] bool listening() const noexcept {
      return listener_ != nullptr && listener_->valid();
   }

   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint() const {
      if (!listening()) {
         return std::nullopt;
      }
      return p2p_endpoint_for(listener_->local_endpoint());
   }

   void listen(fcl::p2p::endpoint endpoint) {
      if (!endpoint.is_direct_tcp()) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      try {
         listener_ =
             std::make_unique<fcl::tcp::listener>(runtime_.context().get_executor(), endpoint.transport);
      } catch (const fcl::exception::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

   void stop() {
      if (listener_) {
         listener_->cancel();
      }
   }

   boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                    const node::connect_options& options) {
      if (!endpoint.is_direct_tcp()) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      try {
         auto tcp = co_await connector_.async_connect_connection(endpoint.transport);
         auto tcp_stream = std::move(tcp).into_transport_stream();
         auto upgraded = co_await upgrade_outbound_stream(
             fcl::p2p::stream{std::move(tcp_stream.stream)}, options_, expected_peer_for(endpoint, options));
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = p2p_endpoint_for(std::move(tcp_stream.local_endpoint)),
             .remote_endpoint = p2p_endpoint_for(std::move(tcp_stream.remote_endpoint)),
         };
      } catch (const fcl::exception::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

   boost::asio::awaitable<connection> async_accept() {
      if (!listening()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "P2P TCP direct listener is not active");
      }
      try {
         auto tcp = co_await listener_->async_accept_connection();
         auto tcp_stream = std::move(tcp).into_transport_stream();
         auto upgraded = co_await upgrade_inbound_stream(fcl::p2p::stream{std::move(tcp_stream.stream)}, options_,
                                                         std::nullopt);
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = p2p_endpoint_for(std::move(tcp_stream.local_endpoint)),
             .remote_endpoint = p2p_endpoint_for(std::move(tcp_stream.remote_endpoint)),
         };
      } catch (const fcl::exception::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

 private:
   fcl::asio::runtime& runtime_;
   const node::options& options_;
   fcl::tcp::connector connector_;
   std::unique_ptr<fcl::tcp::listener> listener_;
};

} // namespace

void register_tcp_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options) {
   auto owned = std::make_shared<tcp_profile>(runtime, options);
   value.add(profile{
       .supports = [owned](const fcl::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoint = [owned] { return owned->local_endpoint(); },
       .listen = [owned](fcl::p2p::endpoint endpoint) { owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_connect =
           [owned](fcl::p2p::endpoint endpoint, const node::connect_options& options) {
              return owned->async_connect(std::move(endpoint), options);
           },
       .async_accept = [owned] { return owned->async_accept(); },
   });
}

} // namespace fcl::p2p::direct
