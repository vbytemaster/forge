module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module fcl.p2p.api;

import fcl.api;
import fcl.p2p.exceptions;
import fcl.p2p.node;
import fcl.p2p.protocol;
import fcl.raw.raw;
import fcl.quic.errors;
import fcl.quic.framed_stream;

export namespace fcl::p2p {

struct api_peer_policy {
   bool require_known_peer = false;
};

struct api_discovery_scope {
   std::string value;
};

class api_binding {
 public:
   using session_handler = std::function<boost::asio::awaitable<void>(fcl::api::session&)>;

   api_binding(node* owner, fcl::api::binding_plan plan, protocol_id protocol, fcl::api::codec_id codec,
               api_peer_policy peer_policy, api_discovery_scope discovery_scope, std::size_t max_inflight_per_peer)
       : owner_{owner}, plan_{std::move(plan)}, protocol_{std::move(protocol)}, codec_{std::move(codec)},
         peer_policy_{std::move(peer_policy)}, discovery_scope_{std::move(discovery_scope)},
         max_inflight_per_peer_{max_inflight_per_peer} {}

   api_binding& on_session(session_handler handler) {
      on_session_ = std::move(handler);
      return *this;
   }

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] protocol_handler handler() const {
      return [binding = *this](incoming_protocol_stream stream) mutable -> boost::asio::awaitable<void> {
         co_await binding.accept(std::move(stream));
      };
   }

   boost::asio::awaitable<fcl::api::session> accept(incoming_protocol_stream stream) const {
      validate_stream(stream);
      auto session = make_session();
      if (on_session_) {
         co_await on_session_(session);
      }
      co_await serve(std::move(stream));
      co_return session;
   }

   boost::asio::awaitable<void> serve(incoming_protocol_stream stream) const {
      validate_stream(stream);
      auto calls = fcl::api::call_runtime{
          fcl::api::call_runtime_options{.max_inflight = max_inflight_per_peer_}};
      auto streams = std::unordered_map<std::uint64_t, std::vector<fcl::api::frame>>{};

      while (true) {
         try {
            auto payload = co_await stream.stream.async_read_frame();
            auto request = fcl::raw::unpack<fcl::api::frame>(payload);
            if (request.codec != codec_) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::codec_failed, "P2P API frame codec is not accepted",
                                   fcl::exception::ctx("codec", request.codec.value));
            }
            if (request.kind == fcl::api::frame_kind::request && grouped_stream_method(request)) {
               calls.observe(request);
               if (streams.size() >= max_inflight_per_peer_) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::resource_exhausted, "P2P API max streams exceeded");
               }
               if (!streams.emplace(request.id.value, std::vector<fcl::api::frame>{std::move(request)}).second) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error, "duplicate active P2P API stream");
               }
               continue;
            }
            if (auto active = streams.find(request.id.value); active != streams.end()) {
               if (request.kind != fcl::api::frame_kind::stream_end) {
                  calls.observe(request);
               }
               active->second.push_back(std::move(request));
               if (active->second.back().kind == fcl::api::frame_kind::stream_end) {
                  auto frames = std::move(active->second);
                  streams.erase(active);
                  auto responses = co_await plan_.dispatch_stream(std::move(frames), calls);
                  co_await write_responses(stream.stream, responses);
               }
               continue;
            }
            auto responses = co_await plan_.dispatch_many(std::move(request), calls);
            co_await write_responses(stream.stream, responses);
         } catch (const fcl::quic::quic_error& error) {
            if (error.kind() == fcl::quic::error_kind::connection_closed ||
                error.kind() == fcl::quic::error_kind::stream_closed ||
                error.kind() == fcl::quic::error_kind::stream_reset ||
                error.kind() == fcl::quic::error_kind::canceled) {
               co_return;
            }
            throw;
         }
      }
   }

   [[nodiscard]] const fcl::api::codec_id& codec() const noexcept {
      return codec_;
   }

   [[nodiscard]] const api_peer_policy& peer_policy() const noexcept {
      return peer_policy_;
   }

   [[nodiscard]] const api_discovery_scope& discovery_scope() const noexcept {
      return discovery_scope_;
   }

   [[nodiscard]] std::size_t max_inflight_per_peer() const noexcept {
      return max_inflight_per_peer_;
   }

 private:
   [[nodiscard]] bool grouped_stream_method(const fcl::api::frame& request) const noexcept {
      const auto* descriptor = plan_.local == nullptr ? nullptr : plan_.local->describe(request.api);
      const auto* method = descriptor == nullptr ? nullptr : fcl::api::find_method(*descriptor, request.method);
      return method != nullptr && (method->kind == fcl::api::method_kind::client_stream ||
                                   method->kind == fcl::api::method_kind::bidirectional_stream);
   }

   static boost::asio::awaitable<void> write_responses(fcl::quic::framed_stream& stream,
                                                       const std::vector<fcl::api::frame>& responses) {
      for (const auto& response : responses) {
         auto response_bytes = fcl::api::bytes{};
         fcl::raw::pack(response_bytes, response);
         co_await stream.async_write_frame(std::span<const std::uint8_t>{response_bytes.data(), response_bytes.size()});
      }
   }

   void validate_stream(const incoming_protocol_stream& stream) const {
      if (stream.protocol != protocol_) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "P2P API binding received wrong protocol",
                             fcl::exception::ctx("protocol", stream.protocol.value));
      }
      if (peer_policy_.require_known_peer) {
         if (owner_ == nullptr || !owner_->peers().find(stream.session.remote_peer).has_value()) {
            FCL_THROW_EXCEPTION(fcl::p2p::exceptions::peer_not_found, "P2P API peer is not known",
                                fcl::exception::ctx("peer", stream.session.remote_peer.value));
         }
      }
   }

   [[nodiscard]] fcl::api::session make_session() const {
      if (plan_.local == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "P2P API binding has no local registry");
      }
      return fcl::api::session{fcl::api::view{*plan_.local}};
   }

   node* owner_ = nullptr;
   fcl::api::binding_plan plan_;
   protocol_id protocol_;
   fcl::api::codec_id codec_;
   api_peer_policy peer_policy_{};
   api_discovery_scope discovery_scope_{};
   std::size_t max_inflight_per_peer_ = 64;
   session_handler on_session_;
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
      codec_ = std::move(value);
      return *this;
   }

   api_builder& peer_policy(api_peer_policy value) {
      peer_policy_ = value;
      return *this;
   }

   api_builder& discovery_scope(api_discovery_scope value) {
      discovery_scope_ = std::move(value);
      return *this;
   }

   api_builder& max_inflight_per_peer(std::size_t value) {
      max_inflight_per_peer_ = value;
      return *this;
   }

   [[nodiscard]] api_binding build() {
      return api_binding{owner_, std::move(plan_), std::move(protocol_), std::move(codec_), peer_policy_,
                         std::move(discovery_scope_), max_inflight_per_peer_};
   }

 private:
   node* owner_ = nullptr;
   fcl::api::binding_plan plan_;
   fcl::p2p::protocol_id protocol_{.value = "/fcl/api/1"};
   fcl::api::codec_id codec_{.value = "fcl.raw"};
   api_peer_policy peer_policy_{};
   api_discovery_scope discovery_scope_{};
   std::size_t max_inflight_per_peer_ = 64;
};

[[nodiscard]] inline api_builder api(node& owner) {
   return api_builder{owner};
}

[[nodiscard]] inline api_builder api() {
   return api_builder{};
}

class route_binding {
 public:
   route_binding(protocol_id protocol, protocol_handler handler)
       : protocol_{std::move(protocol)}, handler_{std::move(handler)} {}

   [[nodiscard]] const protocol_id& protocol() const noexcept {
      return protocol_;
   }

   [[nodiscard]] const protocol_handler& handler() const noexcept {
      return handler_;
   }

 private:
   protocol_id protocol_;
   protocol_handler handler_;
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

   route_builder& handler(protocol_handler value) {
      handler_ = std::move(value);
      return *this;
   }

   [[nodiscard]] route_binding build() {
      if (protocol_.value.empty()) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "P2P route protocol id must not be empty");
      }
      if (!handler_) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "P2P route handler must not be empty",
                             fcl::exception::ctx("protocol", protocol_.value));
      }
      return route_binding{std::move(protocol_), std::move(handler_)};
   }

 private:
   fcl::p2p::protocol_id protocol_;
   protocol_handler handler_;
};

[[nodiscard]] inline route_builder route() {
   return {};
}

} // namespace fcl::p2p
