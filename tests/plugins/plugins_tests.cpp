#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import fcl.api;
import fcl.api.transport;
import fcl.app;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.p2p;
import fcl.plugins;
import fcl.schema;

namespace {

struct plugin_log {
   std::vector<std::string> entries;
};

std::string_view test_certificate() {
   return "-----BEGIN CERTIFICATE-----\n"
          "MIICpDCCAYwCCQCJjaEDxrQqBzANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAkx\n"
          "MjcuMC4wLjEwHhcNMjYwNDI5MDgwMTMzWhcNMjYwNDMwMDgwMTMzWjAUMRIwEAYD\n"
          "VQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDy\n"
          "sbPH/R4QUz725sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TX\n"
          "gl9tHkNpKmI92s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+\n"
          "x7MRWXfKYd/ArGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOS\n"
          "lI/lDqIjZxo7jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuv\n"
          "M+mTj6eO4UQ42w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXj\n"
          "nPOZzBinLRTDnE59HbDZAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAHSOUQTEDgjC\n"
          "uwza9ayfThJTs43j+TziWHLlowqCiHt/ipRNFEW7L0ibTnbMdQBFGfaLkTAhc5Rd\n"
          "6O6x+9o76pgEYxEg0rDkgNXmprNmS+nL7Are+iiF6R+X8dts3MQgtONPApAXE96P\n"
          "/n5K4GDQTd3WCI37hkmJA6rmwziFDTlwqtKWts39g8PqAbXac27rVR/iD0gWdOws\n"
          "qiaoGj/0WW9qcgjYGdCc0/CbbnyiWbi48VVf0yyfm7wgcz90byaKIQchHdb/qjyU\n"
          "wy7nfU5TJ5MKQ5yeqPTWmPYZZp9TKa5VD6wZD/IH7jH3GdJ/fSyroVLZktVnmxJa\n"
          "dmG/9wwivwQ=\n"
          "-----END CERTIFICATE-----\n";
}

std::string_view test_private_key() {
   return "-----BEGIN PRIVATE KEY-----\n"
          "MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDysbPH/R4QUz72\n"
          "5sY376knXjSDCA+O5+Udwqfl4qaXHTAooWfplVY/WFRCnnMV6+TXgl9tHkNpKmI9\n"
          "2s4O/LuJ5xnCCPX8k5i70gSnaGpClYSx+0gix8QgddDDsbLbIU/+x7MRWXfKYd/A\n"
          "rGNelPMadlvmcoEhumVUAwjYSV26GhNAmUacJlho3ltyujYSGFOSlI/lDqIjZxo7\n"
          "jbAGMMpiyu1omQ5nxjTm+bfOTcksBRMQP8mDz0vYXHXirA+xDfuvM+mTj6eO4UQ4\n"
          "2w+iVLqhSPEhfLURmR4NULtPmq9hT7d1wS/Ys9q4Hj/j+kcXRCXjnPOZzBinLRTD\n"
          "nE59HbDZAgMBAAECggEBAIWVjHhy+V5RA+JRCh/12ayirNLG2BF30OP9pf7iL4IT\n"
          "/dMPbKvkmDGLw+1bW8tgKXj5+N6N/trfCm4zhqI3OF7ihooH9qYM88/F/OvMjFiU\n"
          "BhMVVhJW1LxtPPjKUcFN58M8VnMhRM9v6gIaoSOJZvpU1abVtgBDocyJUxAB6gYp\n"
          "i7MzoRwHGsL5mW/luE5H92/S8NNwLWBDA7DIGfrTZ6POf92h5I5W3CuTcqR5FICz\n"
          "3pfU3i443yZmsmkc9duH2gZ9cb9j4pRtNLbbsGmRVrBlgnkVFk8JWbikc8MpLeKO\n"
          "VKP7A2NvxJIrc7oFYrf4hbw8P70YL7S9B3W3yBPPzJECgYEA+Y3nG8CtvVTE/Keo\n"
          "qb5Rljlnj9DEffrylLyYUYfSSNR4Olc2WCPBiz0rPCDdO0VGeXAwqLf2VP7IEyAx\n"
          "kvrnqhzHWMhiLv+k4tIVyKCwpuofN0JsoUCi7CwRf+H2Pg+t6ewLV116THKsd41H\n"
          "IRElWyEvZsmbbhlLrsxUtfFZWnUCgYEA+PZwXUn+cb8kRmfG959gMawTtcfvnBUX\n"
          "sIn7LQl/ZWUIiLMWCaS3FbqkiGjaEYo6om1invYNJNA9zp/ECauSDp58NICCL0ie\n"
          "L7z26sEa6Ocg2VdR4ezpN3cM6dyAKfTFGb9V6qjyqNIPCE4eey6ZJ+CU/mpEfSDu\n"
          "+RGMzfdDCFUCgYEA5FRUn0zk6jU0YyMXq+9pgLSXL7vI/Kdt6m7AQuCto1tbga2o\n"
          "GG7mt/pIo6RCJufUemoO62AeL1hKQU2UbjHJYxkfv/jf9LaM68dijQWRe7b8xres\n"
          "4sFcEBCmFkbt4YzBCCWjntT1gBrv+Ba4fOXOMxoi374Yy1yzpYRpAWuI4L0CgYAn\n"
          "u1SlXrivuHx2i/tR62pzou2mVhkkRK16LBsczeY57UzWXBZJRbM+UYIOjwU2RWQk\n"
          "JebWTZg9ZspmXlLv5CS0FpDl5BhiqWktXy/cuSKtRq2UYf4cWy3A/0vdSqZdi8Wk\n"
          "3Uc94uaPEK77eVQd/orMtWexzo3NlmLs9uMMv8g/3QKBgQCbik0UoJkkqNRMmWG8\n"
          "dKQzj58eRI8fmKdJlWNfj2QMspd2vXMbsWYgAbFbU1QcVs1n8PxNydM+cfy77w8q\n"
          "NWMlYP7rUFQ3ekYWqrRlshZdJ/h24PALd1nPCvhc4C9dvn+zW3BLVez1lBuFO8n8\n"
          "0YkgmTgW7Ieibqnf4DqYp//nkw==\n"
          "-----END PRIVATE KEY-----\n";
}

[[nodiscard]] fcl::p2p::peer_id test_peer(std::uint8_t seed) {
   return fcl::p2p::make_peer_id(
      {.type = fcl::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, seed)});
}

[[nodiscard]] fcl::config::document test_p2p_config(std::optional<fcl::p2p::peer_id> peer = std::nullopt) {
   auto document = fcl::config::document{};
   document.set("p2p.allow-insecure-test-mode", true);
   document.set("p2p.certificate-pem", std::string{test_certificate()});
   document.set("p2p.private-key-pem", std::string{test_private_key()});
   if (peer.has_value()) {
      document.set("p2p.peer-id", peer->to_string());
   }
   return document;
}

class node_test_api {
 public:
   virtual ~node_test_api() = default;
   virtual boost::asio::awaitable<int> ping(int request) = 0;

   static fcl::api::descriptor describe() {
      return fcl::api::contract<node_test_api>({.id = {"node.test"}, .version = {.major = 1, .revision = 0}})
         .method<&node_test_api::ping, int, int>("ping")
         .build();
   }
};

class node_test_api_impl final : public node_test_api {
 public:
   boost::asio::awaitable<int> ping(int request) override {
      co_return request + 1;
   }
};

class route_publisher_plugin final : public fcl::app::plugin {
 public:
   explicit route_publisher_plugin(plugin_log& log) : log_{&log} {}

   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "route-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p_node::api>(
         {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});

      auto plan = fcl::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      p2p->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"});
      p2p->publish_protocol(
         fcl::p2p::protocol_id{.value = "/product/blob-transfer/1"},
         [](fcl::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
            co_return;
         });
      log_->entries.push_back("routes.published");
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      log_->entries.push_back("routes.startup");
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      log_->entries.push_back("routes.shutdown");
      co_return;
   }

 private:
   plugin_log* log_ = nullptr;
};

class duplicate_route_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "duplicate-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p_node::api>(
         {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
      auto handler = [](fcl::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
         co_return;
      };
      p2p->publish_protocol(fcl::p2p::protocol_id{.value = "/product/duplicate/1"}, handler);
      p2p->publish_protocol(fcl::p2p::protocol_id{.value = "/product/duplicate/1"}, handler);
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class p2p_plugin_application final : public fcl::app::application_shell {
 public:
   explicit p2p_plugin_application(plugin_log& log) : log_{&log} {}

   protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "route-publisher"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
         .factory = [this] {
            return std::make_unique<route_publisher_plugin>(*log_);
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      co_return;
   }

 private:
   plugin_log* log_ = nullptr;
};

class duplicate_p2p_plugin_application final : public fcl::app::application_shell {
   protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "duplicate-route"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
         .factory = [] {
            return std::make_unique<duplicate_route_plugin>();
         },
      });
   }
};

class p2p_only_application final : public fcl::app::application_shell {
   protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
   }
};

[[nodiscard]] const fcl::config::field_descriptor& require_field(const fcl::config::component_descriptor& descriptor,
                                                                 std::string_view name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

[[nodiscard]] bool has_field(const fcl::config::component_descriptor& descriptor, std::string_view name) {
   return std::ranges::any_of(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
}

} // namespace

BOOST_AUTO_TEST_CASE(p2p_node_plugin_config_is_described_from_public_schema) {
   auto plugin = fcl::plugins::p2p_node{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "p2p");

   const auto& listen = require_field(*descriptor, "listen");
   BOOST_TEST(static_cast<int>(listen.kind) == static_cast<int>(fcl::schema::value_kind::string_list));
   BOOST_TEST(listen.has_default);

   const auto& bootstrap = require_field(*descriptor, "bootstrap");
   BOOST_TEST(static_cast<int>(bootstrap.kind) == static_cast<int>(fcl::schema::value_kind::string_list));
   BOOST_TEST(bootstrap.has_default);

   const auto& private_key = require_field(*descriptor, "private-key-pem");
   BOOST_TEST(private_key.secret);

   const auto& max_inflight = require_field(*descriptor, "max-inflight-per-peer");
   BOOST_TEST(max_inflight.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_inflight.default_value.storage) == 64U);

   const auto& api_deadline = require_field(*descriptor, "api.deadline-ms");
   BOOST_TEST(api_deadline.has_default);
   BOOST_TEST(std::get<std::uint64_t>(api_deadline.default_value.storage) == 0U);

   const auto& api_frame_size = require_field(*descriptor, "api.max-frame-size");
   BOOST_TEST(api_frame_size.has_default);
   BOOST_TEST(std::get<std::uint64_t>(api_frame_size.default_value.storage) == 16U * 1024U * 1024U);

   const auto& insecure = require_field(*descriptor, "allow-insecure-test-mode");
   BOOST_TEST(insecure.has_default);
   BOOST_TEST(!std::get<bool>(insecure.default_value.storage));

   const auto& path_policy = require_field(*descriptor, "path.policy");
   BOOST_TEST(path_policy.has_default);
   BOOST_TEST(std::get<std::string>(path_policy.default_value.storage) == "direct-preferred");

   const auto& relay_trust = require_field(*descriptor, "relay.trust");
   BOOST_TEST(relay_trust.has_default);
   BOOST_TEST(std::get<std::string>(relay_trust.default_value.storage) == "known-only");

   BOOST_TEST(!has_field(*descriptor, "delivery.outbox-mode"));
   BOOST_TEST(!has_field(*descriptor, "delivery.queue-limit"));
   BOOST_TEST(!has_field(*descriptor, "retry.max-attempts"));
   BOOST_TEST(!has_field(*descriptor, "retry.deadline-ms"));
   BOOST_TEST(!has_field(*descriptor, "maintenance.peer-exchange-interval-ms"));
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_publishes_safe_local_api_for_route_contributions) {
   auto log = plugin_log{};
   auto app = p2p_plugin_application{log};

   app.configure(test_p2p_config());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   BOOST_TEST(app.apis().describe({.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0}) != nullptr);
   BOOST_TEST(log.entries == (std::vector<std::string>{"routes.published", "routes.startup"}),
              boost::test_tools::per_element());

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
   BOOST_TEST(log.entries == (std::vector<std::string>{"routes.published", "routes.startup", "routes.shutdown"}),
              boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_duplicate_protocol_contributions_before_startup) {
   auto app = duplicate_p2p_plugin_application{};

   app.configure(test_p2p_config());
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.initialize()),
                     fcl::plugins::p2p_node::exceptions::route_conflict);
}

BOOST_AUTO_TEST_CASE(p2p_node_api_rejects_facade_calls_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto plugin = fcl::plugins::p2p_node{};
   auto apis = fcl::api::registry{};
   auto provider = fcl::api::installer{apis};
   fcl::asio::blocking::run(runtime, plugin.provide(provider));

   auto p2p = apis.get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)p2p->local_peer(), fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->local_endpoint(), fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->local_endpoints(), fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->network_info(), fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime,
                                              p2p->remote<node_test_api>(
                                                 test_peer(10), fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"})),
                     fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_listens_from_config_and_exposes_local_endpoints) {
   const auto local_peer = test_peer(20);
   auto config = test_p2p_config(local_peer);
   config.set("p2p.listen",
              fcl::config::value::array_type{fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"},
                                             fcl::config::value{"/ip4/127.0.0.1/tcp/0"}});

   auto app = p2p_only_application{};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   const auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto endpoint = p2p->local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   BOOST_CHECK_EQUAL(endpoint->transport.host, "127.0.0.1");
   BOOST_CHECK_NE(endpoint->transport.port, 0);
   BOOST_TEST(endpoint->peer->to_string() == local_peer.to_string());

   const auto endpoints = p2p->local_endpoints();
   BOOST_REQUIRE_EQUAL(endpoints.size(), 2U);
   BOOST_TEST(endpoints[0].peer->to_string() == local_peer.to_string());
   BOOST_TEST(endpoints[1].peer->to_string() == local_peer.to_string());

   const auto info = p2p->network_info();
   BOOST_TEST(info.local_peer.to_string() == local_peer.to_string());
   BOOST_TEST(info.started);
   BOOST_TEST(info.local_endpoints.size() == endpoints.size());

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_opens_remote_api_over_p2p_stream) {
   const auto server_peer = test_peer(30);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto log = plugin_log{};
   auto server = p2p_plugin_application{log};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   const auto client_peer = test_peer(31);
   auto client_config = test_p2p_config(client_peer);
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});

   auto client = p2p_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto client_p2p = client.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto remote = fcl::asio::blocking::run(
      client.runtime(),
      client_p2p->remote<node_test_api>(server_p2p->local_peer(),
                                        fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"}));
   const auto response = fcl::asio::blocking::run(
      client.runtime(),
      remote.call<int, int>({.id = {"node.test"}, .major = 1, .min_revision = 0}, "ping", 41));
   BOOST_TEST(response == 42);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config();
      config.set("p2p.max-inflight-per-peer", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.listen", fcl::config::value::array_type{fcl::config::value{"127.0.0.1:0"}});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.api.max-frame-size", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.path.policy", std::string{"teleport"});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.path.policy", std::string{"relay-only"});
      auto app = p2p_only_application{};
      BOOST_CHECK_NO_THROW(app.configure(config));
   }
}
