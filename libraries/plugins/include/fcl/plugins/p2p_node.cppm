module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module fcl.plugins.p2p_node;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.p2p;
import fcl.quic.endpoint;
import fcl.quic.framed_stream;
import fcl.schema;

export namespace fcl::plugins {

class p2p_node final : public fcl::app::plugin {
 public:
   struct config {
      std::vector<std::string> listen;
      std::vector<std::string> bootstrap;
      std::vector<std::string> advertised_endpoints;
      std::string peer_id;
      std::string certificate_pem;
      std::string private_key_pem;
      std::string api_codec = "fcl.raw";
      std::uint64_t max_inflight_per_peer = 64;
      std::uint64_t max_sessions = 1024;
      std::uint64_t max_protocol_handlers = 1024;
      bool allow_insecure_test_mode = false;
   };

   class api;

   p2p_node();
   ~p2p_node() override;

   p2p_node(const p2p_node&) = delete;
   p2p_node& operator=(const p2p_node&) = delete;

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

class p2p_node::api {
 public:
   struct send_options {
      fcl::p2p::open_options open{};
   };

   struct broadcast_options {
      std::vector<fcl::p2p::peer_id> peers;
      send_options send{};
   };

   struct send_result {
      fcl::p2p::peer_id peer;
      bool sent = false;
      std::string error;
   };

   struct broadcast_result {
      fcl::p2p::peer_id peer;
      bool sent = false;
      std::string error;
   };

   virtual ~api() = default;

   [[nodiscard]] static fcl::api::descriptor describe();

   [[nodiscard]] virtual fcl::p2p::peer_id local_peer() const = 0;
   [[nodiscard]] virtual std::optional<fcl::quic::endpoint> local_endpoint() const = 0;
   [[nodiscard]] virtual fcl::p2p::node_metrics metrics() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::peer_record> peers() const = 0;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) = 0;
   virtual void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) = 0;

   virtual boost::asio::awaitable<fcl::p2p::session_info>
   connect(fcl::quic::endpoint endpoint, fcl::p2p::connect_options options = {}) = 0;

   virtual boost::asio::awaitable<fcl::quic::framed_stream>
   open_protocol_stream(fcl::p2p::peer_id peer, fcl::p2p::protocol_id protocol,
                        fcl::p2p::open_options options = {}) = 0;

   virtual boost::asio::awaitable<void> request_peer_exchange(fcl::p2p::peer_id peer) = 0;
   virtual boost::asio::awaitable<fcl::p2p::reachability_state> probe_reachability(fcl::p2p::peer_id peer) = 0;

   boost::asio::awaitable<send_result> send(fcl::p2p::peer_id peer, fcl::p2p::message message);
   virtual boost::asio::awaitable<send_result> send(fcl::p2p::peer_id peer, fcl::p2p::message message,
                                                    send_options options) = 0;

   boost::asio::awaitable<std::vector<broadcast_result>> broadcast(fcl::p2p::message message);
   virtual boost::asio::awaitable<std::vector<broadcast_result>>
   broadcast(fcl::p2p::message message, broadcast_options options) = 0;

 private:
   friend class p2p_node;
   class impl;
};

[[nodiscard]] fcl::app::plugin_descriptor p2p_node_descriptor();

} // namespace fcl::plugins

BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_node::config, (),
                      (listen, bootstrap, advertised_endpoints, peer_id, certificate_pem, private_key_pem, api_codec,
                       max_inflight_per_peer, max_sessions, max_protocol_handlers, allow_insecure_test_mode))

export template <> struct fcl::schema::rules<fcl::plugins::p2p_node::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_node::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_node::config>();
      schema.field<&fcl::plugins::p2p_node::config::listen>("listen")
         .default_value(std::vector<std::string>{})
         .description("QUIC listen endpoints, for example quic://0.0.0.0:9443");
      schema.field<&fcl::plugins::p2p_node::config::bootstrap>("bootstrap")
         .default_value(std::vector<std::string>{})
         .description("Bootstrap peer endpoints to connect on startup");
      schema.field<&fcl::plugins::p2p_node::config::advertised_endpoints>("advertised-endpoints")
         .default_value(std::vector<std::string>{});
      schema.field<&fcl::plugins::p2p_node::config::peer_id>("peer-id").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::certificate_pem>("certificate-pem").default_value("");
      schema.field<&fcl::plugins::p2p_node::config::private_key_pem>("private-key-pem").default_value("").secret();
      schema.field<&fcl::plugins::p2p_node::config::api_codec>("api-codec").default_value("fcl.raw");
      schema.field<&fcl::plugins::p2p_node::config::max_inflight_per_peer>("max-inflight-per-peer")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::max_sessions>("max-sessions")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::max_protocol_handlers>("max-protocol-handlers")
         .default_value(std::uint64_t{1024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_node::config::allow_insecure_test_mode>("allow-insecure-test-mode")
         .default_value(false)
         .description("Test-only mode for local development without mTLS material");
      return schema;
   }
};
