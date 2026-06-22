module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

export module fcl.p2p.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.transport.api.exceptions;
import fcl.transport.api.options;
import fcl.transport.api.client;
import fcl.transport.api.connection;
import fcl.transport.api.server;
import fcl.p2p.exceptions;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.p2p.stream;

export namespace fcl::p2p {

class api_binding {
 public:
   struct peer_policy {
      bool require_known_peer = false;
   };

   struct discovery_scope {
      std::string value;
   };

   api_binding(node* owner, fcl::api::binding_plan plan, protocol_id protocol, fcl::transport::api::options options,
               peer_policy peer_policy_value, discovery_scope discovery_scope_value)
       : owner_{owner}, plan_{std::move(plan)}, protocol_{std::move(protocol)}, options_{std::move(options)},
         peer_policy_{std::move(peer_policy_value)}, discovery_scope_{std::move(discovery_scope_value)} {}

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] node::protocol_handler handler() const {
      return [binding = *this](node::incoming_protocol_stream stream) mutable -> boost::asio::awaitable<void> {
         co_await binding.accept(std::move(stream));
      };
   }

   boost::asio::awaitable<void> accept(node::incoming_protocol_stream stream) const {
      validate_stream(stream);
      auto trusted = fcl::api::metadata{
         fcl::api::metadata_entry{
            .key = std::string{fcl::api::p2p_remote_peer_metadata_key},
            .value = stream.session.remote_peer.to_string(),
         },
      };
      co_await fcl::transport::api::serve_stream(std::move(stream.stream).into_transport_stream(), plan_, options_,
                                                 std::move(trusted));
   }

   boost::asio::awaitable<void> serve(node::incoming_protocol_stream stream) const {
      co_await accept(std::move(stream));
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return options_.codec;
   }

   [[nodiscard]] const peer_policy& peer_policy_value() const noexcept {
      return peer_policy_;
   }

   [[nodiscard]] const discovery_scope& discovery_scope_value() const noexcept {
      return discovery_scope_;
   }

   [[nodiscard]] std::size_t max_inflight_per_peer() const noexcept {
      return options_.max_inflight;
   }

   [[nodiscard]] const fcl::transport::api::options& options() const noexcept {
      return options_;
   }

 private:
   void validate_stream(const node::incoming_protocol_stream& stream) const {
      if (stream.protocol != protocol_) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "P2P API binding received wrong protocol",
                             fcl::exceptions::ctx("protocol", stream.protocol.value));
      }
      if (peer_policy_.require_known_peer) {
         if (owner_ == nullptr || !owner_->peers().find(stream.session.remote_peer).has_value()) {
            FCL_THROW_EXCEPTION(fcl::p2p::exceptions::peer_not_found, "P2P API peer is not known",
                                fcl::exceptions::ctx("peer", stream.session.remote_peer.value));
         }
      }
   }

   node* owner_ = nullptr;
   fcl::api::binding_plan plan_;
   protocol_id protocol_;
   fcl::transport::api::options options_;
   peer_policy peer_policy_{};
   discovery_scope discovery_scope_{};
};

class api_builder {
 public:
   api_builder() = default;
   explicit api_builder(node& owner) : owner_{&owner} {}

   api_builder& use(fcl::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& protocol_id(std::string value) {
      protocol_ = fcl::p2p::protocol_id{.value = std::move(value)};
      return *this;
   }

   api_builder& protocol_id(fcl::p2p::protocol_id value) {
      protocol_ = std::move(value);
      return *this;
   }

   api_builder& codec(fcl::api::codec_id value) {
      options_.codec = std::move(value);
      return *this;
   }

   api_builder& peer_policy(api_binding::peer_policy value) {
      peer_policy_ = value;
      return *this;
   }

   api_builder& discovery_scope(api_binding::discovery_scope value) {
      discovery_scope_ = std::move(value);
      return *this;
   }

   api_builder& max_inflight_per_peer(std::size_t value) {
      options_.max_inflight = value;
      return *this;
   }

   api_builder& deadline(std::chrono::milliseconds value) {
      options_.deadline = value;
      return *this;
   }

   api_builder& max_frame_size(std::uint32_t value) {
      options_.max_frame_size = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{owner_, std::move(plan_), std::move(protocol_), options_, peer_policy_,
                         std::move(discovery_scope_)};
   }

 private:
   node* owner_ = nullptr;
   fcl::api::binding_plan plan_;
   fcl::p2p::protocol_id protocol_{.value = "/fcl/api/1"};
   fcl::transport::api::options options_{.max_inflight = 64};
   api_binding::peer_policy peer_policy_{};
   api_binding::discovery_scope discovery_scope_{};
};

[[nodiscard]] inline api_builder api(node& owner) {
   return api_builder{owner};
}

[[nodiscard]] inline api_builder api() {
   return api_builder{};
}

class route_binding {
 public:
   route_binding(protocol_id protocol, node::protocol_handler handler)
       : protocol_{std::move(protocol)}, handler_{std::move(handler)} {}

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] const node::protocol_handler& handler() const noexcept {
      return handler_;
   }

 private:
   protocol_id protocol_;
   node::protocol_handler handler_;
};

class route_builder {
 public:
   route_builder& protocol_id(std::string value) {
      protocol_ = fcl::p2p::protocol_id{.value = std::move(value)};
      return *this;
   }

   route_builder& protocol_id(fcl::p2p::protocol_id value) {
      protocol_ = std::move(value);
      return *this;
   }

   route_builder& handler(node::protocol_handler value) {
      handler_ = std::move(value);
      return *this;
   }

   [[nodiscard]] route_binding build() {
      return route_binding{std::move(protocol_), std::move(handler_)};
   }

 private:
   fcl::p2p::protocol_id protocol_{.value = "/fcl/route/1"};
   node::protocol_handler handler_;
};

[[nodiscard]] inline route_builder route() {
   return {};
}

} // namespace fcl::p2p
