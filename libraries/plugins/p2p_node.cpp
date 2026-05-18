module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.p2p;

namespace fcl::plugins {
namespace {

[[nodiscard]] fcl::p2p::peer_id default_test_peer() {
   return fcl::p2p::peer_id{.value = "0000000000000000000000000000000000000000000000000000000000000001"};
}

[[nodiscard]] bool contains_protocol(const std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::protocol_handler>>& routes,
                                     const fcl::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
      return route.first == protocol;
   });
}

[[nodiscard]] std::vector<fcl::quic::endpoint> parse_endpoint_list(const std::vector<std::string>& values) {
   auto out = std::vector<fcl::quic::endpoint>{};
   out.reserve(values.size());
   for (const auto& value : values) {
      out.push_back(fcl::quic::parse_endpoint(value));
   }
   return out;
}

[[nodiscard]] p2p_node::config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<p2p_node::config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P node config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      throw std::invalid_argument{std::move(message)};
   }
   return std::move(decoded.value);
}

} // namespace

struct p2p_node::impl : public std::enable_shared_from_this<p2p_node::impl> {
   fcl::p2p::node_options options{
      .explicit_peer_id = default_test_peer(),
      .allow_insecure_test_mode = false,
   };
   fcl::api::codec_id api_codec{.value = "fcl.raw"};
   std::size_t max_inflight_per_peer = 64;
   std::vector<fcl::quic::endpoint> listen;
   std::vector<fcl::quic::endpoint> bootstrap;
   std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::protocol_handler>> routes;
   fcl::p2p::node* raw = nullptr;
   std::unique_ptr<fcl::p2p::node> node;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] fcl::p2p::node& require_node() {
      if (!node) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::invalid_options, "P2P node plugin is not initialized");
      }
      return *node;
   }

   void add_route(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) {
      if (started) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol,
                             "P2P routes must be published before p2p_node startup",
                             fcl::exception::ctx("protocol", protocol.value));
      }
      if (protocol.value.empty() || !handler) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "P2P route is invalid");
      }
      if (contains_protocol(routes, protocol)) {
         FCL_THROW_EXCEPTION(fcl::p2p::exceptions::unsupported_protocol, "duplicate P2P route",
                             fcl::exception::ctx("protocol", protocol.value));
      }
      routes.emplace_back(std::move(protocol), std::move(handler));
   }
};

class p2p_node::api::impl final : public p2p_node::api {
 public:
   explicit impl(std::shared_ptr<p2p_node::impl> impl) : impl_{std::move(impl)} {}

   fcl::p2p::peer_id local_peer() const override {
      return impl_->require_node().local_peer();
   }

   std::optional<fcl::quic::endpoint> local_endpoint() const override {
      return impl_->require_node().local_endpoint();
   }

   fcl::p2p::node_metrics metrics() const override {
      return impl_->require_node().metrics();
   }

   std::vector<fcl::p2p::peer_record> peers() const override {
      return impl_->require_node().peers().snapshot();
   }

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) override {
      auto binding = fcl::p2p::api()
                        .use(std::move(plan))
                        .protocol_id(protocol)
                        .codec(impl_->api_codec)
                        .max_inflight_per_peer(impl_->max_inflight_per_peer)
                        .build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) override {
      auto binding = fcl::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   boost::asio::awaitable<fcl::p2p::session_info>
   connect(fcl::quic::endpoint endpoint, fcl::p2p::connect_options options) override {
      co_return co_await impl_->require_node().async_connect(std::move(endpoint), std::move(options));
   }

   boost::asio::awaitable<fcl::quic::framed_stream>
   open_protocol_stream(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol,
                        fcl::p2p::open_options options) override {
      co_return co_await impl_->require_node().async_open_protocol_stream(std::move(peer), std::move(protocol),
                                                                          std::move(options));
   }

   boost::asio::awaitable<void> request_peer_exchange(fcl::p2p::peer_id peer) override {
      co_await impl_->require_node().async_request_peer_exchange(std::move(peer));
   }

   boost::asio::awaitable<fcl::p2p::reachability_state> probe_reachability(fcl::p2p::peer_id peer) override {
      co_return co_await impl_->require_node().async_probe_reachability(std::move(peer));
   }

   boost::asio::awaitable<send_result> send(fcl::p2p::peer_id peer, fcl::p2p::message message,
                                            send_options options) override {
      try {
         auto stream = co_await impl_->require_node().async_open_protocol_stream(peer, message.protocol(),
                                                                                 std::move(options.open));
         co_await stream.async_write_frame(std::span<const std::uint8_t>{message.data().data(), message.data().size()});
         co_return send_result{.peer = std::move(peer), .sent = true};
      } catch (const std::exception& error) {
         co_return send_result{.peer = std::move(peer), .sent = false, .error = error.what()};
      }
   }

   boost::asio::awaitable<std::vector<broadcast_result>>
   broadcast(fcl::p2p::message message, broadcast_options options) override {
      auto peers = std::move(options.peers);
      if (peers.empty()) {
         for (const auto& peer : impl_->require_node().peers().snapshot()) {
            peers.push_back(peer.peer);
         }
      }

      auto out = std::vector<broadcast_result>{};
      out.reserve(peers.size());
      for (auto& peer : peers) {
         auto result = co_await send(peer, message, options.send);
         out.push_back(broadcast_result{
            .peer = std::move(result.peer),
            .sent = result.sent,
            .error = std::move(result.error),
         });
      }
      co_return out;
   }

 private:
   std::shared_ptr<p2p_node::impl> impl_;
};

fcl::api::descriptor p2p_node::api::describe() {
   return fcl::api::contract<p2p_node::api>({.id = {"fcl.plugins.p2p_node"}, .version = {.major = 1, .revision = 0}})
      .build();
}

boost::asio::awaitable<p2p_node::api::send_result> p2p_node::api::send(fcl::p2p::peer_id peer,
                                                                        fcl::p2p::message message) {
   co_return co_await send(std::move(peer), std::move(message), send_options{});
}

boost::asio::awaitable<std::vector<p2p_node::api::broadcast_result>>
p2p_node::api::broadcast(fcl::p2p::message message) {
   co_return co_await broadcast(std::move(message), broadcast_options{});
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
   impl_->api_codec = fcl::api::codec_id{.value = config.api_codec};
   impl_->listen = parse_endpoint_list(config.listen);
   impl_->bootstrap = parse_endpoint_list(config.bootstrap);
   impl_->options.advertised_endpoints = parse_endpoint_list(config.advertised_endpoints);
   impl_->options.certificate_pem = config.certificate_pem;
   impl_->options.private_key_pem = config.private_key_pem;
   const auto& peer_id = config.peer_id;
   impl_->options.explicit_peer_id = peer_id.empty() ? default_test_peer() : fcl::p2p::peer_id{.value = peer_id};
   impl_->max_inflight_per_peer = static_cast<std::size_t>(config.max_inflight_per_peer);
   impl_->options.limits.max_sessions = static_cast<std::size_t>(config.max_sessions);
   impl_->options.limits.max_protocol_handlers = static_cast<std::size_t>(config.max_protocol_handlers);
   impl_->options.allow_insecure_test_mode = config.allow_insecure_test_mode;
   co_return;
}

boost::asio::awaitable<void> p2p_node::provide(fcl::api::provider& provider) {
   provider.install<p2p_node::api>(p2p_node::api::describe(), std::make_shared<p2p_node::api::impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> p2p_node::initialize(fcl::app::plugin_context& context) {
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
      (void)co_await node.async_connect(endpoint);
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
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->started = false;
}

fcl::app::plugin_descriptor p2p_node_descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_node"},
      .factory = [] {
         return std::make_unique<p2p_node>();
      },
   };
}

} // namespace fcl::plugins
