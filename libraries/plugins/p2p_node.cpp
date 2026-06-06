module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node;

import fcl.api;
import fcl.api.transport;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p;

namespace fcl::plugins {
namespace {

struct parsed_policy {
   fcl::p2p::path::policy path{};
   bool relay_client_enabled = true;
   bool relay_server_enabled = false;
   bool relay_public_allowed = false;
   std::chrono::milliseconds relay_reservation_ttl{60'000};
   std::size_t relay_max_candidates = 4;
};

[[nodiscard]] fcl::p2p::peer_id default_test_peer() {
   return fcl::p2p::make_peer_id(
      {.type = fcl::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, 1)});
}

[[nodiscard]] bool contains_protocol(
   const std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::node::protocol_handler>>& routes,
   const fcl::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
      return route.first == protocol;
   });
}

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

[[nodiscard]] p2p_node::config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<p2p_node::config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P node config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, message);
   }
   return std::move(decoded.value);
}

void validate_relay_trust(const std::string& value) {
   if (value == "known-only" || value == "public-allowed") {
      return;
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, "invalid P2P relay trust policy",
                       fcl::exceptions::ctx("trust", value));
}

[[nodiscard]] fcl::p2p::path::policy parse_path_policy(const std::string& value, bool relay_client_enabled,
                                                       std::size_t relay_max_candidates) {
   if (relay_max_candidates == 0) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, "P2P relay candidate limit must be positive");
   }
   if (value == "direct-only") {
      return fcl::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = false,
         .allow_relay = false,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   if (value == "direct-preferred") {
      return fcl::p2p::path::policy{
         .allow_direct = true,
         .allow_hole_punch = relay_client_enabled,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   if (value == "relay-only") {
      return fcl::p2p::path::policy{
         .allow_direct = false,
         .allow_hole_punch = false,
         .allow_relay = relay_client_enabled,
         .max_relay_candidates = relay_max_candidates,
      };
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, "invalid P2P path policy",
                       fcl::exceptions::ctx("policy", value));
}

[[nodiscard]] parsed_policy parse_policy(const p2p_node::config& config) {
   validate_relay_trust(config.relay_trust);
   const auto relay_max_candidates = static_cast<std::size_t>(config.relay_max_candidates);
   return parsed_policy{
      .path = parse_path_policy(config.path_policy, config.relay_client_enabled, relay_max_candidates),
      .relay_client_enabled = config.relay_client_enabled,
      .relay_server_enabled = config.relay_server_enabled,
      .relay_public_allowed = config.relay_public_allowed || config.relay_trust == "public-allowed",
      .relay_reservation_ttl = to_ms(config.relay_reservation_ttl_ms),
      .relay_max_candidates = relay_max_candidates,
   };
}

[[nodiscard]] std::vector<fcl::p2p::endpoint> parse_endpoint_list(const std::vector<std::string>& values) {
   auto out = std::vector<fcl::p2p::endpoint>{};
   out.reserve(values.size());
   for (const auto& value : values) {
      try {
         out.push_back(fcl::p2p::parse_endpoint(value));
      } catch (const std::exception& error) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, "invalid P2P endpoint",
                             fcl::exceptions::ctx("endpoint", value), fcl::exceptions::ctx("error", error.what()));
      }
   }
   return out;
}

} // namespace

struct p2p_node::impl : public std::enable_shared_from_this<p2p_node::impl> {
   fcl::p2p::node::options options{
      .explicit_peer_id = default_test_peer(),
      .allow_insecure_test_mode = false,
   };
   fcl::api::transport::options api_options{
      .codec = fcl::api::codec_id{.value = "fcl.raw"},
      .max_inflight = 64,
   };
   parsed_policy policy{};
   std::vector<fcl::p2p::endpoint> listen;
   std::vector<fcl::p2p::endpoint> bootstrap;
   std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::node::protocol_handler>> routes;
   fcl::p2p::node* raw = nullptr;
   std::unique_ptr<fcl::p2p::node> node;
   fcl::asio::runtime* runtime = nullptr;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] fcl::p2p::node& require_node() {
      if (!node) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
      }
      return *node;
   }

   [[nodiscard]] const fcl::p2p::node& require_node() const {
      if (!node) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
      }
      return *node;
   }

   void add_route(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) {
      if (started) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "P2P routes must be published before startup",
                             fcl::exceptions::ctx("protocol", protocol.value));
      }
      if (protocol.value.empty() || !handler) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "P2P route is invalid");
      }
      if (contains_protocol(routes, protocol)) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "duplicate P2P route",
                             fcl::exceptions::ctx("protocol", protocol.value));
      }
      routes.emplace_back(std::move(protocol), std::move(handler));
   }

   [[nodiscard]] fcl::p2p::node::open_options open_options_for(remote_options value) const {
      if (value.open_deadline.count() <= 0) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_config, "P2P remote open deadline must be positive");
      }
      return fcl::p2p::node::open_options{
         .allow_relay = policy.relay_client_enabled && policy.path.allow_relay,
         .timeout = value.open_deadline,
         .direct_attempt_timeout = std::min(std::chrono::milliseconds{2'000}, value.open_deadline),
         .relay_attempt_timeout = std::min(std::chrono::milliseconds{5'000}, value.open_deadline),
         .max_direct_endpoints = policy.path.max_direct_endpoints,
         .max_relay_candidates = policy.path.max_relay_candidates,
         .allow_hole_punch = policy.relay_client_enabled && policy.path.allow_hole_punch,
      };
   }
};

class p2p_node::api::impl final : public p2p_node::api {
 public:
   explicit impl(std::shared_ptr<p2p_node::impl> impl) : impl_{std::move(impl)} {}

   fcl::p2p::peer_id local_peer() const override {
      return impl_->require_node().local_peer();
   }

   std::optional<fcl::p2p::endpoint> local_endpoint() const override {
      return impl_->require_node().local_endpoint();
   }

   std::vector<fcl::p2p::endpoint> local_endpoints() const override {
      return impl_->require_node().local_endpoints();
   }

   p2p_node::info network_info() const override {
      return p2p_node::info{
         .local_peer = impl_->require_node().local_peer(),
         .local_endpoints = impl_->require_node().local_endpoints(),
         .started = impl_->started,
      };
   }

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) override {
      publish_api(std::move(plan), std::move(protocol), impl_->api_options);
   }

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                    fcl::api::transport::options options) override {
      auto binding = fcl::p2p::api()
                        .use(std::move(plan))
                        .protocol_id(protocol)
                        .codec(options.codec)
                        .max_inflight_per_peer(options.max_inflight)
                        .deadline(options.deadline)
                        .max_frame_size(options.max_frame_size)
                        .build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::node::protocol_handler handler) override {
      auto binding = fcl::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   boost::asio::awaitable<fcl::api::transport::remote>
   remote(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol, fcl::api::descriptor descriptor,
          remote_options options) override {
      auto stream = co_await impl_->require_node().async_open_protocol_stream(std::move(peer), std::move(protocol),
                                                                               impl_->open_options_for(options));
      auto client = fcl::api::transport::client{std::move(stream).into_transport_stream(), impl_->api_options};
      co_return fcl::api::transport::remote{std::move(client), std::move(descriptor)};
   }

 private:
   std::shared_ptr<p2p_node::impl> impl_;
};

fcl::api::descriptor p2p_node::api::describe() {
   return fcl::api::contract<p2p_node::api>({.id = {"fcl.plugins.p2p_node"}, .version = {.major = 1, .revision = 0}})
      .build();
}

p2p_node::p2p_node() : impl_{std::make_shared<impl>()} {}
p2p_node::~p2p_node() = default;

fcl::app::plugin_id p2p_node::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_node"};
}

std::string p2p_node::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> p2p_node::describe_config() const {
   return fcl::config::describe_component<p2p_node::config>("p2p");
}

boost::asio::awaitable<void> p2p_node::configure(fcl::config::component_view view) {
   const auto config = decode_config(view);
   impl_->policy = parse_policy(config);
   impl_->api_options = fcl::api::transport::options{
      .codec = fcl::api::codec_id{.value = config.api_codec},
      .max_inflight = static_cast<std::size_t>(config.max_inflight_per_peer),
      .deadline = to_ms(config.api_deadline_ms),
      .max_frame_size = static_cast<std::uint32_t>(config.api_max_frame_size),
   };
   impl_->listen = parse_endpoint_list(config.listen);
   impl_->bootstrap = parse_endpoint_list(config.bootstrap);
   impl_->options.advertised_endpoints = parse_endpoint_list(config.advertised_endpoints);
   impl_->options.certificate_pem = config.certificate_pem;
   impl_->options.private_key_pem = config.private_key_pem;
   impl_->options.capabilities = fcl::p2p::capability_set{
      .bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange,
   };
   if (impl_->policy.relay_client_enabled) {
      impl_->options.capabilities.add(fcl::p2p::capabilities::hole_punching);
   }
   if (impl_->policy.relay_server_enabled) {
      impl_->options.capabilities.add(fcl::p2p::capabilities::relay);
      impl_->options.capabilities.add(fcl::p2p::capabilities::relay_reservation);
   }
   impl_->options.path_policy = impl_->policy.path;
   impl_->options.relay_policy.client_enabled = impl_->policy.relay_client_enabled;
   impl_->options.relay_policy.service_enabled = impl_->policy.relay_server_enabled;
   impl_->options.relay_policy.public_relay_allowed = impl_->policy.relay_public_allowed;
   impl_->options.relay_policy.max_candidates_per_refresh = impl_->policy.relay_max_candidates;
   impl_->options.relay_policy.target_reservations =
      std::min(impl_->options.relay_policy.target_reservations, impl_->policy.relay_max_candidates);
   if (impl_->options.relay_policy.target_reservations == 0) {
      impl_->options.relay_policy.target_reservations = 1;
   }
   impl_->options.limits.relay.reservation_ttl = impl_->policy.relay_reservation_ttl;
   const auto& peer_id = config.peer_id;
   impl_->options.explicit_peer_id = peer_id.empty() ? default_test_peer() : fcl::p2p::peer_id{.value = peer_id};
   impl_->options.limits.max_sessions = static_cast<std::size_t>(config.max_sessions);
   impl_->options.limits.session_low_watermark =
      std::min(impl_->options.limits.session_low_watermark, impl_->options.limits.max_sessions);
   impl_->options.limits.max_inbound_sessions =
      std::min(impl_->options.limits.max_inbound_sessions, impl_->options.limits.max_sessions);
   impl_->options.limits.max_outbound_sessions =
      std::min(impl_->options.limits.max_outbound_sessions, impl_->options.limits.max_sessions);
   impl_->options.limits.max_protocol_handlers = static_cast<std::size_t>(config.max_protocol_handlers);
   impl_->options.allow_insecure_test_mode = config.allow_insecure_test_mode;
   co_return;
}

boost::asio::awaitable<void> p2p_node::provide(fcl::api::provider& provider) {
   provider.install<p2p_node::api>(p2p_node::api::describe(), std::make_shared<p2p_node::api::impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> p2p_node::initialize(fcl::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->stopping = false;
   impl_->node = std::make_unique<fcl::p2p::node>(context.scheduler().runtime_context(), impl_->options);
   impl_->raw = impl_->node.get();
   co_return;
}

boost::asio::awaitable<void> p2p_node::startup() {
   auto& node = impl_->require_node();
   for (auto& route : impl_->routes) {
      node.register_protocol_handler(route.first, route.second);
   }
   for (const auto& endpoint : impl_->listen) {
      co_await node.async_listen(endpoint);
   }
   for (const auto& endpoint : impl_->bootstrap) {
      try {
         (void)co_await node.async_connect(endpoint);
      } catch (...) {
         fcl::exceptions::capture_and_log("P2P bootstrap connect failed");
      }
   }
   impl_->started = true;
}

void p2p_node::request_stop() noexcept {
   impl_->stopping = true;
   if (impl_->raw) {
      impl_->raw->stop();
   }
}

boost::asio::awaitable<void> p2p_node::shutdown() {
   request_stop();
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->started = false;
}

fcl::app::plugin_descriptor p2p_node::descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_node"},
      .factory = [] {
         return std::make_unique<p2p_node>();
      },
   };
}

} // namespace fcl::plugins
