module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

export module forge.api.p2p.binding;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.api.stream.options;
import forge.api.stream.server;
import forge.net.p2p.exceptions;
import forge.net.p2p.node;
import forge.net.p2p.protocol;
import forge.net.p2p.stream;

export namespace forge::api::p2p {

class api_binding {
 public:
   struct peer_policy {
      bool require_known_peer = false;
   };

   struct discovery_scope {
      std::string value;
   };

   api_binding(forge::net::p2p::node* owner, forge::api::core::binding_plan plan,
               forge::net::p2p::protocol_id protocol, forge::api::stream::options options,
               peer_policy peer_policy_value, discovery_scope discovery_scope_value)
       : owner_{owner}, plan_{std::move(plan)}, protocol_{std::move(protocol)}, options_{std::move(options)},
         peer_policy_{std::move(peer_policy_value)}, discovery_scope_{std::move(discovery_scope_value)} {}

   [[nodiscard]] const forge::net::p2p::protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] forge::net::p2p::node::protocol_handler handler() const {
      return [binding = *this](forge::net::p2p::node::incoming_protocol_stream stream) mutable -> boost::asio::awaitable<void> {
         co_await binding.accept(std::move(stream));
      };
   }

   boost::asio::awaitable<void> accept(forge::net::p2p::node::incoming_protocol_stream stream) const {
      validate_stream(stream);
      auto trusted = forge::api::core::metadata{
         forge::api::core::metadata_entry{
            .key = std::string{forge::api::core::p2p_remote_peer_metadata_key},
            .value = stream.session.remote_peer.to_string(),
         },
      };
      co_await forge::api::stream::serve_stream(std::move(stream.stream).into_transport_stream(), plan_, options_,
                                               std::move(trusted));
   }

   boost::asio::awaitable<void> serve(forge::net::p2p::node::incoming_protocol_stream stream) const {
      co_await accept(std::move(stream));
   }

   [[nodiscard]] const forge::api::core::codec_id& codec() const noexcept {
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

   [[nodiscard]] const forge::api::stream::options& options() const noexcept {
      return options_;
   }

 private:
   void validate_stream(const forge::net::p2p::node::incoming_protocol_stream& stream) const {
      if (stream.protocol != protocol_) {
         FORGE_THROW_EXCEPTION(forge::net::p2p::exceptions::unsupported_protocol, "P2P API binding received wrong protocol",
                             forge::exceptions::ctx("protocol", stream.protocol.value));
      }
      if (peer_policy_.require_known_peer) {
         if (owner_ == nullptr || !owner_->peers().find(stream.session.remote_peer).has_value()) {
            FORGE_THROW_EXCEPTION(forge::net::p2p::exceptions::peer_not_found, "P2P API peer is not known",
                                forge::exceptions::ctx("peer", stream.session.remote_peer.value));
         }
      }
   }

   forge::net::p2p::node* owner_ = nullptr;
   forge::api::core::binding_plan plan_;
   forge::net::p2p::protocol_id protocol_;
   forge::api::stream::options options_;
   peer_policy peer_policy_{};
   discovery_scope discovery_scope_{};
};

class api_builder {
 public:
   api_builder() = default;
   explicit api_builder(forge::net::p2p::node& owner) : owner_{&owner} {}

   api_builder& use(forge::api::core::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   api_builder& protocol_id(std::string value) {
      protocol_ = forge::net::p2p::protocol_id{.value = std::move(value)};
      return *this;
   }

   api_builder& protocol_id(forge::net::p2p::protocol_id value) {
      protocol_ = std::move(value);
      return *this;
   }

   api_builder& codec(forge::api::core::codec_id value) {
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
   forge::net::p2p::node* owner_ = nullptr;
   forge::api::core::binding_plan plan_;
   forge::net::p2p::protocol_id protocol_{.value = "/forge/api/2"};
   forge::api::stream::options options_{.max_inflight = 64};
   api_binding::peer_policy peer_policy_{};
   api_binding::discovery_scope discovery_scope_{};
};

[[nodiscard]] inline api_builder api(forge::net::p2p::node& owner) {
   return api_builder{owner};
}

[[nodiscard]] inline api_builder api() {
   return api_builder{};
}

class route_binding {
 public:
   route_binding(forge::net::p2p::protocol_id protocol, forge::net::p2p::node::protocol_handler handler)
       : protocol_{std::move(protocol)}, handler_{std::move(handler)} {}

   [[nodiscard]] const forge::net::p2p::protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] const forge::net::p2p::node::protocol_handler& handler() const noexcept {
      return handler_;
   }

 private:
   forge::net::p2p::protocol_id protocol_;
   forge::net::p2p::node::protocol_handler handler_;
};

class route_builder {
 public:
   route_builder& protocol_id(std::string value) {
      protocol_ = forge::net::p2p::protocol_id{.value = std::move(value)};
      return *this;
   }

   route_builder& protocol_id(forge::net::p2p::protocol_id value) {
      protocol_ = std::move(value);
      return *this;
   }

   route_builder& handler(forge::net::p2p::node::protocol_handler value) {
      handler_ = std::move(value);
      return *this;
   }

   [[nodiscard]] route_binding build() {
      return route_binding{std::move(protocol_), std::move(handler_)};
   }

 private:
   forge::net::p2p::protocol_id protocol_{.value = "/forge/route/1"};
   forge::net::p2p::node::protocol_handler handler_;
};

[[nodiscard]] inline route_builder route() {
   return {};
}

} // namespace forge::api::p2p
