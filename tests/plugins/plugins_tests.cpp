#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import fcl.api;
import fcl.app;
import fcl.asio.blocking;
import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.p2p;
import fcl.plugins;
import fcl.quic.errors;
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

[[nodiscard]] fcl::config::document test_p2p_config() {
   auto document = fcl::config::document{};
   document.set("p2p.allow-insecure-test-mode", true);
   document.set("p2p.certificate-pem", std::string{test_certificate()});
   document.set("p2p.private-key-pem", std::string{test_private_key()});
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
         [](fcl::p2p::incoming_protocol_stream) -> boost::asio::awaitable<void> {
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
      auto handler = [](fcl::p2p::incoming_protocol_stream) -> boost::asio::awaitable<void> {
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
      registry.register_plugin(fcl::plugins::p2p_node_descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "route-publisher"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
         .factory = [this] {
            return std::make_unique<route_publisher_plugin>(*log_);
         },
      });
   }

   boost::asio::awaitable<void> on_install_ports(fcl::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      co_return;
   }

 private:
   plugin_log* log_ = nullptr;
};

class duplicate_p2p_plugin_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node_descriptor());
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
      registry.register_plugin(fcl::plugins::p2p_node_descriptor());
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

   const auto& insecure = require_field(*descriptor, "allow-insecure-test-mode");
   BOOST_TEST(insecure.has_default);
   BOOST_TEST(!std::get<bool>(insecure.default_value.storage));
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
                     fcl::p2p::exceptions::unsupported_protocol);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_listens_from_config_and_exposes_local_endpoint) {
   auto config = test_p2p_config();
   config.set("p2p.listen", fcl::config::value::array_type{fcl::config::value{"quic://127.0.0.1:0"}});

   auto app = p2p_only_application{};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   const auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto endpoint = p2p->local_endpoint();
   BOOST_REQUIRE(endpoint.has_value());
   BOOST_CHECK_EQUAL(endpoint->host, "127.0.0.1");
   BOOST_CHECK_NE(endpoint->port, 0);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config();
      config.set("p2p.max-inflight-per-peer", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), std::invalid_argument);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.listen", fcl::config::value::array_type{fcl::config::value{"127.0.0.1:0"}});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::quic::quic_error);
   }
}
