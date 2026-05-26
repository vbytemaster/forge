#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import fcl.api;
import fcl.app;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.p2p;
import fcl.plugins;
import fcl.quic.exceptions;
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

class fake_outbox_store final : public fcl::plugins::p2p_node::outbox_store {
 public:
   boost::asio::awaitable<fcl::plugins::p2p_node::delivery_id>
   enqueue(fcl::plugins::p2p_node::outbox_record record) override {
      auto id = record.id;
      if (id.value == 0) {
         id.value = next_id_++;
      }
      record.id = id;
      record.state = fcl::plugins::p2p_node::delivery_state::queued;
      records_.insert_or_assign(id.value, std::move(record));
      ++enqueue_count;
      co_return id;
   }

   boost::asio::awaitable<std::vector<fcl::plugins::p2p_node::outbox_record>>
   claim_due(std::size_t limit, std::chrono::steady_clock::time_point now) override {
      auto out = std::vector<fcl::plugins::p2p_node::outbox_record>{};
      for (auto& [ignored, record] : records_) {
         static_cast<void>(ignored);
         if (out.size() >= limit) {
            break;
         }
         if (record.state == fcl::plugins::p2p_node::delivery_state::queued && record.next_attempt_at <= now) {
            record.state = fcl::plugins::p2p_node::delivery_state::in_flight;
            out.push_back(record);
         }
      }
      co_return out;
   }

   boost::asio::awaitable<void> release(fcl::plugins::p2p_node::outbox_record record) override {
      record.state = fcl::plugins::p2p_node::delivery_state::queued;
      records_.insert_or_assign(record.id.value, std::move(record));
      ++release_count;
      co_return;
   }

   boost::asio::awaitable<void> mark_attempt(fcl::plugins::p2p_node::outbox_record record) override {
      record.state = fcl::plugins::p2p_node::delivery_state::in_flight;
      records_.insert_or_assign(record.id.value, std::move(record));
      ++attempt_count;
      co_return;
   }

   boost::asio::awaitable<void>
   mark_delivered(fcl::plugins::p2p_node::delivery_result result) override {
      auto& record = records_.at(result.id.value);
      record.state = fcl::plugins::p2p_node::delivery_state::delivered;
      record.last_error = result.error;
      ++delivered_count;
      co_return;
   }

   boost::asio::awaitable<void>
   mark_failed(fcl::plugins::p2p_node::delivery_result result) override {
      auto& record = records_.at(result.id.value);
      record.state = result.state;
      record.last_error = result.error;
      ++failed_count;
      co_return;
   }

   boost::asio::awaitable<std::optional<fcl::plugins::p2p_node::delivery_snapshot>>
   get(fcl::plugins::p2p_node::delivery_id id) const override {
      const auto found = records_.find(id.value);
      if (found == records_.end()) {
         co_return std::nullopt;
      }
      const auto& record = found->second;
      co_return fcl::plugins::p2p_node::delivery_snapshot{
         .id = record.id,
         .peer = record.peer,
         .protocol = record.message.protocol(),
         .state = record.state,
         .attempts = record.attempts,
         .error = record.last_error,
      };
   }

   boost::asio::awaitable<void> cancel(fcl::plugins::p2p_node::delivery_id id) override {
      auto& record = records_.at(id.value);
      record.state = fcl::plugins::p2p_node::delivery_state::cancelled;
      ++cancel_count;
      co_return;
   }

   boost::asio::awaitable<std::optional<std::chrono::steady_clock::time_point>> next_due() const override {
      auto value = std::optional<std::chrono::steady_clock::time_point>{};
      for (const auto& [ignored, record] : records_) {
         static_cast<void>(ignored);
         if (record.state != fcl::plugins::p2p_node::delivery_state::queued) {
            continue;
         }
         if (!value || record.next_attempt_at < *value) {
            value = record.next_attempt_at;
         }
      }
      co_return value;
   }

   std::size_t enqueue_count = 0;
   std::size_t attempt_count = 0;
   std::size_t release_count = 0;
   std::size_t delivered_count = 0;
   std::size_t failed_count = 0;
   std::size_t cancel_count = 0;

   [[nodiscard]] const fcl::plugins::p2p_node::outbox_record&
   record(fcl::plugins::p2p_node::delivery_id id) const {
      return records_.at(id.value);
   }

   [[nodiscard]] fcl::plugins::p2p_node::delivery_id seed(fcl::plugins::p2p_node::outbox_record record) {
      if (record.id.value == 0) {
         record.id.value = next_id_++;
      }
      const auto id = record.id;
      records_.insert_or_assign(id.value, std::move(record));
      return id;
   }

 private:
   std::uint64_t next_id_ = 1;
   std::map<std::uint64_t, fcl::plugins::p2p_node::outbox_record> records_;
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

class p2p_with_outbox_application final : public fcl::app::application_shell {
 public:
   explicit p2p_with_outbox_application(std::shared_ptr<fake_outbox_store> outbox) : outbox_{std::move(outbox)} {}

   protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<fcl::plugins::p2p_node::outbox_store>(
         fcl::plugins::p2p_node::outbox_store::describe(), outbox_);
      co_return;
   }

 private:
   std::shared_ptr<fake_outbox_store> outbox_;
};

[[nodiscard]] const fcl::config::field_descriptor& require_field(const fcl::config::component_descriptor& descriptor,
                                                                 std::string_view name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

boost::asio::awaitable<void> wait_for(std::chrono::milliseconds duration) {
   auto executor = co_await boost::asio::this_coro::executor;
   auto timer = boost::asio::steady_timer{executor};
   timer.expires_after(duration);
   co_await timer.async_wait(boost::asio::use_awaitable);
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

   const auto& outbox_mode = require_field(*descriptor, "delivery.outbox-mode");
   BOOST_TEST(outbox_mode.has_default);
   BOOST_TEST(std::get<std::string>(outbox_mode.default_value.storage) == "memory");

   const auto& max_attempts = require_field(*descriptor, "retry.max-attempts");
   BOOST_TEST(max_attempts.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_attempts.default_value.storage) == 3U);

   const auto& path_policy = require_field(*descriptor, "path.policy");
   BOOST_TEST(path_policy.has_default);
   BOOST_TEST(std::get<std::string>(path_policy.default_value.storage) == "direct-preferred");

   const auto& relay_trust = require_field(*descriptor, "relay.trust");
   BOOST_TEST(relay_trust.has_default);
   BOOST_TEST(std::get<std::string>(relay_trust.default_value.storage) == "known-only");
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

BOOST_AUTO_TEST_CASE(p2p_node_plugin_requires_external_outbox_when_configured) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});

   auto app = p2p_only_application{};
   app.configure(config);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.initialize()),
                     fcl::plugins::p2p_node::exceptions::outbox_required);
}

BOOST_AUTO_TEST_CASE(p2p_node_api_rejects_delivery_calls_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto plugin = fcl::plugins::p2p_node{};
   auto apis = fcl::api::registry{};
   auto provider = fcl::api::installer{apis};
   fcl::asio::blocking::run(runtime, plugin.provide(provider));

   auto p2p = apis.get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/pre-init/1"},
      fcl::api::bytes{1},
   };

   BOOST_CHECK_THROW(fcl::asio::blocking::run(
                        runtime, p2p->send_async(fcl::p2p::peer_id{.value = "peer-a"}, std::move(message))),
                     fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->delivery(fcl::plugins::p2p_node::delivery_id{.value = 1}),
                     fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime,
                                              p2p->cancel(fcl::plugins::p2p_node::delivery_id{.value = 1})),
                     fcl::plugins::p2p_node::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_uses_consumer_provided_outbox_store) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});

   auto outbox = std::make_shared<fake_outbox_store>();
   auto app = p2p_with_outbox_application{outbox};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/test/1"},
      fcl::api::bytes{1, 2, 3},
   };
   const auto delivery = fcl::asio::blocking::run(
      app.runtime(),
      p2p->send_async(fcl::p2p::peer_id{.value = "peer-a"}, std::move(message),
                      fcl::plugins::p2p_node::send_options{
                         .reliability = fcl::plugins::p2p_node::delivery_reliability::durable_retry,
                      }));

   BOOST_TEST(delivery.id().value != 0U);
   BOOST_TEST(outbox->enqueue_count == 1U);

   const auto snapshot = fcl::asio::blocking::run(app.runtime(), delivery.snapshot());
   BOOST_REQUIRE(snapshot.has_value());
   BOOST_TEST(snapshot->id.value == delivery.id().value);
   BOOST_TEST(snapshot->protocol.value == "/product/test/1");

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_applies_configured_delivery_defaults) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});
   config.set("p2p.retry.max-attempts", std::uint64_t{7});
   config.set("p2p.retry.deadline-ms", std::uint64_t{12'345});
   config.set("p2p.retry.jitter", false);
   config.set("p2p.path.policy", std::string{"direct-only"});

   auto outbox = std::make_shared<fake_outbox_store>();
   auto app = p2p_with_outbox_application{outbox};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/defaults/1"},
      fcl::api::bytes{7, 8, 9},
   };

   const auto delivery = fcl::asio::blocking::run(
      app.runtime(), p2p->send_async(fcl::p2p::peer_id{.value = "peer-config"}, std::move(message)));

   const auto& record = outbox->record(delivery.id());
   BOOST_TEST(record.options.max_attempts == 7U);
   BOOST_TEST(record.options.deadline == std::chrono::milliseconds{12'345});
   BOOST_TEST(static_cast<int>(record.options.path) ==
              static_cast<int>(fcl::plugins::p2p_node::path_policy::direct_only));
   BOOST_TEST(!record.options.jitter);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_delivery_handle_can_cancel_async_delivery) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});

   auto outbox = std::make_shared<fake_outbox_store>();
   auto app = p2p_with_outbox_application{outbox};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/cancel/1"},
      fcl::api::bytes{1, 2},
   };

   auto delivery = fcl::asio::blocking::run(
      app.runtime(), p2p->send_async(fcl::p2p::peer_id{.value = "peer-cancel"}, std::move(message)));
   fcl::asio::blocking::run(app.runtime(), delivery.cancel());

   const auto snapshot = fcl::asio::blocking::run(app.runtime(), delivery.snapshot());
   BOOST_REQUIRE(snapshot.has_value());
   BOOST_TEST(static_cast<int>(snapshot->state) ==
              static_cast<int>(fcl::plugins::p2p_node::delivery_state::cancelled));
   BOOST_TEST(outbox->cancel_count == 1U);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_broadcast_async_returns_delivery_handles_per_peer) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});

   auto outbox = std::make_shared<fake_outbox_store>();
   auto app = p2p_with_outbox_application{outbox};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/broadcast-async/1"},
      fcl::api::bytes{3, 4},
   };

   const auto deliveries = fcl::asio::blocking::run(
      app.runtime(),
      p2p->broadcast_async(std::move(message),
                           fcl::plugins::p2p_node::broadcast_options{
                              .peers =
                                 {
                                    fcl::p2p::peer_id{.value = "peer-one"},
                                    fcl::p2p::peer_id{.value = "peer-two"},
                                 },
                           }));

   BOOST_TEST(deliveries.size() == 2U);
   BOOST_TEST(deliveries[0].id().value != 0U);
   BOOST_TEST(deliveries[1].id().value != 0U);
   BOOST_TEST(deliveries[0].id().value != deliveries[1].id().value);
   BOOST_TEST(outbox->enqueue_count == 2U);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_resumes_due_records_from_external_outbox_on_startup) {
   auto config = test_p2p_config();
   config.set("p2p.delivery.outbox-mode", std::string{"external-required"});
   config.set("p2p.delivery.worker-batch", std::uint64_t{1});

   auto outbox = std::make_shared<fake_outbox_store>();
   auto record = fcl::plugins::p2p_node::outbox_record{
      .peer = fcl::p2p::peer_id{.value = "missing-peer"},
      .message = fcl::p2p::message{
         fcl::p2p::protocol_id{.value = "/product/resume/1"},
         fcl::api::bytes{9},
      },
      .options =
         fcl::plugins::p2p_node::send_options{
            .path = fcl::plugins::p2p_node::path_policy::direct_only,
            .deadline = std::chrono::milliseconds{50},
            .initial_backoff = std::chrono::milliseconds{1},
            .max_backoff = std::chrono::milliseconds{1},
            .max_attempts = 1,
         },
      .next_attempt_at = std::chrono::steady_clock::now(),
   };
   const auto id = outbox->seed(std::move(record));

   auto app = p2p_with_outbox_application{outbox};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());
   fcl::asio::blocking::run(app.runtime(), wait_for(std::chrono::milliseconds{25}));

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto delivery = p2p->delivery(id);
   const auto snapshot = fcl::asio::blocking::run(app.runtime(), delivery.snapshot());
   BOOST_REQUIRE(snapshot.has_value());
   BOOST_TEST(static_cast<int>(snapshot->state) ==
              static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));
   BOOST_TEST(outbox->failed_count == 1U);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_exposes_semantic_send_options_for_consumers) {
   auto options = fcl::plugins::p2p_node::send_options{};
   options.reliability = fcl::plugins::p2p_node::delivery_reliability::bounded_retry;
   options.path = fcl::plugins::p2p_node::path_policy::direct_only;
   options.max_attempts = 2;
   options.deadline = std::chrono::milliseconds{1'000};
   BOOST_TEST(options.max_attempts == 2U);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_relay_only_until_engine_supports_no_direct_open) {
   auto app = p2p_only_application{};
   app.configure(test_p2p_config());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/relay-only/1"},
      fcl::api::bytes{1},
   };

   const auto result = fcl::asio::blocking::run(
      app.runtime(),
      p2p->send(fcl::p2p::peer_id{.value = "peer-relay"}, std::move(message),
                fcl::plugins::p2p_node::send_options{
                   .path = fcl::plugins::p2p_node::path_policy::relay_only,
                   .deadline = std::chrono::milliseconds{50},
                }));

   BOOST_TEST(static_cast<int>(result.state) == static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));
   BOOST_TEST(result.error_identity.category == "fcl.plugins.p2p_node");
   BOOST_TEST(result.error_identity.code ==
              static_cast<std::uint32_t>(fcl::plugins::p2p_node::exceptions::code::relay_policy_denied));

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_retries_retryable_delivery_failures) {
   auto app = p2p_only_application{};
   app.configure(test_p2p_config());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/retry/1"},
      fcl::api::bytes{4, 5, 6},
   };

   const auto result = fcl::asio::blocking::run(
      app.runtime(),
      p2p->send(fcl::p2p::peer_id{.value = "missing-peer"}, std::move(message),
                fcl::plugins::p2p_node::send_options{
                   .reliability = fcl::plugins::p2p_node::delivery_reliability::bounded_retry,
                   .path = fcl::plugins::p2p_node::path_policy::direct_only,
                   .deadline = std::chrono::milliseconds{50},
                   .initial_backoff = std::chrono::milliseconds{1},
                   .max_backoff = std::chrono::milliseconds{1},
                   .max_attempts = 2,
                }));

   BOOST_TEST(static_cast<int>(result.state) == static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));
   BOOST_TEST(result.attempts == 2U);
   BOOST_TEST(result.error_identity.category == "fcl.p2p");

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_broadcast_waits_for_terminal_results) {
   auto app = p2p_only_application{};
   app.configure(test_p2p_config());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/broadcast/1"},
      fcl::api::bytes{12},
   };

   const auto results = fcl::asio::blocking::run(
      app.runtime(),
      p2p->broadcast(std::move(message),
                     fcl::plugins::p2p_node::broadcast_options{
                        .peers =
                           {
                              fcl::p2p::peer_id{.value = "missing-a"},
                              fcl::p2p::peer_id{.value = "missing-b"},
                           },
                        .send =
                           fcl::plugins::p2p_node::send_options{
                              .path = fcl::plugins::p2p_node::path_policy::direct_only,
                              .deadline = std::chrono::milliseconds{50},
                              .initial_backoff = std::chrono::milliseconds{1},
                              .max_backoff = std::chrono::milliseconds{1},
                              .max_attempts = 1,
                           },
                     }));

   BOOST_TEST(results.size() == 2U);
   BOOST_TEST(static_cast<int>(results[0].state) ==
              static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));
   BOOST_TEST(static_cast<int>(results[1].state) ==
              static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_delivery_result_waits_for_terminal_state) {
   auto app = p2p_only_application{};
   app.configure(test_p2p_config());
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto p2p = app.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto message = fcl::p2p::message{
      fcl::p2p::protocol_id{.value = "/product/async-result/1"},
      fcl::api::bytes{10, 11},
   };

   auto delivery = fcl::asio::blocking::run(
      app.runtime(),
      p2p->send_async(fcl::p2p::peer_id{.value = "missing-peer"}, std::move(message),
                      fcl::plugins::p2p_node::send_options{
                         .reliability = fcl::plugins::p2p_node::delivery_reliability::bounded_retry,
                         .path = fcl::plugins::p2p_node::path_policy::direct_only,
                         .deadline = std::chrono::milliseconds{50},
                         .initial_backoff = std::chrono::milliseconds{1},
                         .max_backoff = std::chrono::milliseconds{1},
                         .max_attempts = 2,
                      }));

   const auto result = fcl::asio::blocking::run(app.runtime(), delivery.result());
   BOOST_TEST(static_cast<int>(result.state) == static_cast<int>(fcl::plugins::p2p_node::delivery_state::failed));
   BOOST_TEST(result.attempts == 2U);
   BOOST_TEST(result.error_identity.category == "fcl.p2p");

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
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
      BOOST_CHECK_THROW(app.configure(config), fcl::exception::base);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.retry.max-attempts", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), std::invalid_argument);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.path.policy", std::string{"teleport"});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::invalid_delivery_policy);
   }

   {
      auto config = test_p2p_config();
      config.set("p2p.path.policy", std::string{"relay-only"});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_node::exceptions::relay_policy_denied);
   }
}
