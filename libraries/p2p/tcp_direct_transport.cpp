module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

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
#include "operation_deadline.hpp"
#include "stream_upgrade.hpp"
#include "tcp_stream_upgrade.hpp"

namespace fcl::p2p::direct {
namespace {

[[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(fcl::transport::endpoint value) {
   return fcl::p2p::endpoint{.transport = std::move(value)};
}

[[nodiscard]] std::string listener_key(fcl::p2p::endpoint value) {
   value.peer.reset();
   return value.to_string();
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

[[noreturn]] void rethrow_tcp_as_p2p(const fcl::exceptions::base& error) {
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

struct cancel_current_scope {
   std::shared_ptr<std::function<void()>> value;

   ~cancel_current_scope() {
      if (!value) {
         return;
      }
      try {
         *value = {};
      } catch (...) {
      }
   }
};

class tcp_profile final {
   struct listener_entry {
      std::unique_ptr<fcl::tcp::listener> value;
      bool active = true;
   };

 public:
   tcp_profile(fcl::asio::runtime& runtime_value, const node::options& options_value)
       : runtime_(runtime_value), options_(options_value) {}

   [[nodiscard]] bool supports(const fcl::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_tcp();
   }

   [[nodiscard]] bool listening() const noexcept {
      return std::ranges::any_of(listeners_, [](const auto& item) {
         return item.second.active;
      });
   }

   [[nodiscard]] std::vector<fcl::p2p::endpoint> local_endpoints() const {
      auto out = std::vector<fcl::p2p::endpoint>{};
      out.reserve(listeners_.size());
      for (const auto& [_, listener] : listeners_) {
         if (listener.active && listener.value->valid()) {
            out.push_back(p2p_endpoint_for(listener.value->local_endpoint()));
         }
      }
      return out;
   }

   fcl::p2p::endpoint listen(fcl::p2p::endpoint endpoint) {
      if (!endpoint.is_direct_tcp()) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      if (endpoint.transport.port != 0) {
         auto found = listeners_.find(listener_key(endpoint));
         if (found != listeners_.end() && found->second.active) {
            FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P TCP direct listener endpoint is already active");
         }
      }
      try {
         auto listener =
             std::make_unique<fcl::tcp::listener>(runtime_.context().get_executor(), endpoint.transport);
         auto local = p2p_endpoint_for(listener->local_endpoint());
         const auto key = listener_key(local);
         auto found = listeners_.find(key);
         if (found != listeners_.end() && found->second.active) {
            FCL_THROW_EXCEPTION(exceptions::invalid_options, "P2P TCP direct listener endpoint is already active");
         }
         listeners_[key] = listener_entry{.value = std::move(listener), .active = true};
         return local;
      } catch (const fcl::exceptions::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

   void stop() {
      for (auto& [_, listener] : listeners_) {
         listener.active = false;
         listener.value->close();
      }
   }

   boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                    const node::connect_options& options) {
      if (!endpoint.is_direct_tcp()) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct TCP endpoint");
      }
      auto expected_peer = expected_peer_for(endpoint, options);
      auto remote_transport = endpoint.transport;
      auto connector = fcl::tcp::connector{runtime_.context().get_executor()};
      auto cancel_current = std::make_shared<std::function<void()>>([&connector] { connector.cancel(); });
      auto deadline = operation_deadline{runtime_.context(), options.timeout};
      auto cancel_scope = cancel_current_scope{cancel_current};
      deadline.arm([cancel_current] {
         if (*cancel_current) {
            (*cancel_current)();
         }
      });
      try {
         auto tcp = co_await connector.async_connect_connection(std::move(remote_transport));
         *cancel_current = [&tcp] { tcp.cancel(); };
         const auto local_endpoint = p2p_endpoint_for(tcp.local_endpoint());
         const auto remote_endpoint = p2p_endpoint_for(tcp.remote_endpoint());
         auto upgraded = co_await upgrade_outbound_tcp(
             std::move(tcp), options_, std::move(expected_peer),
             tcp_upgrade_deadline{.context = &runtime_.context(), .timeout = options.timeout, .cancel_current = cancel_current});
         if (!deadline.finish()) {
            throw_operation_timeout("P2P TCP direct connect");
         }
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const fcl::exceptions::base& error) {
         if (deadline.timed_out()) {
            throw_operation_timeout("P2P TCP direct connect");
         }
         rethrow_tcp_as_p2p(error);
      }
   }

   boost::asio::awaitable<connection> async_accept(fcl::p2p::endpoint endpoint) {
      auto found = listeners_.find(listener_key(std::move(endpoint)));
      if (found == listeners_.end() || !found->second.active || !found->second.value->valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "P2P TCP direct listener is not active");
      }
      try {
         auto tcp = co_await found->second.value->async_accept_connection();
         const auto local_endpoint = p2p_endpoint_for(tcp.local_endpoint());
         const auto remote_endpoint = p2p_endpoint_for(tcp.remote_endpoint());
         auto cancel_current = std::make_shared<std::function<void()>>([&tcp] { tcp.cancel(); });
         auto deadline = operation_deadline{runtime_.context(), node::connect_options{}.timeout};
         auto cancel_scope = cancel_current_scope{cancel_current};
         deadline.arm([cancel_current] {
            if (*cancel_current) {
               (*cancel_current)();
            }
         });
         auto upgraded = upgraded_session{};
         try {
            upgraded = co_await upgrade_inbound_tcp(
                std::move(tcp), options_, std::nullopt,
                tcp_upgrade_deadline{.context = &runtime_.context(),
                                     .timeout = node::connect_options{}.timeout,
                                     .cancel_current = cancel_current});
            if (!deadline.finish()) {
               throw_operation_timeout("P2P TCP direct accept");
            }
         } catch (const fcl::exceptions::base&) {
            if (deadline.timed_out()) {
               throw_operation_timeout("P2P TCP direct accept");
            }
            throw;
         }
         co_return connection{
             .peer = std::move(upgraded.peer),
             .session = std::move(*upgraded.session).as_transport(),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const fcl::exceptions::base& error) {
         rethrow_tcp_as_p2p(error);
      }
   }

 private:
   fcl::asio::runtime& runtime_;
   const node::options& options_;
   std::map<std::string, listener_entry> listeners_;
};

} // namespace

void register_tcp_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options) {
   auto owned = std::make_shared<tcp_profile>(runtime, options);
   value.add(profile{
       .supports = [owned](const fcl::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoints = [owned] { return owned->local_endpoints(); },
       .listen = [owned](fcl::p2p::endpoint endpoint) { return owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_connect =
           [owned](fcl::p2p::endpoint endpoint, const node::connect_options& options) {
              return owned->async_connect(std::move(endpoint), options);
           },
       .async_accept = [owned](fcl::p2p::endpoint endpoint) { return owned->async_accept(std::move(endpoint)); },
   });
}

} // namespace fcl::p2p::direct
