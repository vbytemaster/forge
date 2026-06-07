#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
import fcl.raw.raw;
import fcl.schema;

struct pubsub_payload {
   std::string text;
   std::uint32_t value = 0;

   bool operator==(const pubsub_payload&) const = default;
};
BOOST_DESCRIBE_STRUCT(pubsub_payload, (), (text, value))

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

struct received_pubsub_messages {
   mutable std::mutex mutex;
   std::vector<fcl::plugins::p2p_pubsub::message> raw;
   std::vector<fcl::plugins::p2p_pubsub::typed_message<pubsub_payload>> typed;
   std::size_t accepted = 0;
   std::size_t rejected = 0;
   std::size_t ignored = 0;

   void push(fcl::plugins::p2p_pubsub::message value, fcl::p2p::pubsub::validation_result result) {
      auto lock = std::scoped_lock{mutex};
      raw.push_back(std::move(value));
      switch (result) {
      case fcl::p2p::pubsub::validation_result::accept:
         ++accepted;
         break;
      case fcl::p2p::pubsub::validation_result::reject:
         ++rejected;
         break;
      case fcl::p2p::pubsub::validation_result::ignore:
         ++ignored;
         break;
      }
   }

   void push(fcl::plugins::p2p_pubsub::typed_message<pubsub_payload> value) {
      auto lock = std::scoped_lock{mutex};
      typed.push_back(std::move(value));
   }

   [[nodiscard]] std::size_t raw_size() const {
      auto lock = std::scoped_lock{mutex};
      return raw.size();
   }

   [[nodiscard]] std::size_t typed_size() const {
      auto lock = std::scoped_lock{mutex};
      return typed.size();
   }
};

bool wait_for_count(const received_pubsub_messages& messages, std::size_t raw, std::size_t typed = 0,
                    std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (std::chrono::steady_clock::now() < deadline) {
      if (messages.raw_size() >= raw && messages.typed_size() >= typed) {
         return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{25});
   }
   return messages.raw_size() >= raw && messages.typed_size() >= typed;
}

template <typename Predicate>
bool wait_for_pubsub_snapshot(const fcl::plugins::p2p_pubsub::api& pubsub, Predicate predicate,
                              std::chrono::milliseconds timeout) {
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (std::chrono::steady_clock::now() < deadline) {
      if (predicate(pubsub.snapshot())) {
         return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{25});
   }
   return predicate(pubsub.snapshot());
}

bool wait_for_pubsub_peer(const fcl::plugins::p2p_pubsub::api& pubsub, std::chrono::milliseconds timeout) {
   return wait_for_pubsub_snapshot(
      pubsub,
      [](const fcl::plugins::p2p_pubsub::snapshot& snapshot) {
         return snapshot.core.peers > 0;
      },
      timeout);
}

class scripted_resolver_api {
 public:
   virtual ~scripted_resolver_api() = default;
   virtual boost::asio::awaitable<fcl::plugins::p2p_api_resolver::response>
   query(fcl::plugins::p2p_api_resolver::query request) = 0;

   static fcl::api::descriptor describe() {
      return fcl::api::contract<scripted_resolver_api>(
                {.id = {"fcl.plugins.p2p_api_resolver.protocol"}, .version = {.major = 1, .revision = 0}})
         .method<&scripted_resolver_api::query, fcl::plugins::p2p_api_resolver::query,
                 fcl::plugins::p2p_api_resolver::response>("query")
         .build();
   }
};

struct scripted_resolver_state {
   std::vector<fcl::plugins::p2p_api_resolver::response> responses;
   std::size_t calls = 0;
};

class scripted_resolver_api_impl final : public scripted_resolver_api {
 public:
   explicit scripted_resolver_api_impl(std::shared_ptr<scripted_resolver_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<fcl::plugins::p2p_api_resolver::response>
   query(fcl::plugins::p2p_api_resolver::query) override {
      const auto index = std::min(state_->calls, state_->responses.size() - 1);
      ++state_->calls;
      co_return state_->responses[index];
   }

 private:
   std::shared_ptr<scripted_resolver_state> state_;
};

[[nodiscard]] fcl::plugins::p2p_api_resolver::entry resolver_test_entry(std::string protocol) {
   return fcl::plugins::p2p_api_resolver::entry{
      .id = {.value = "node.test"},
      .version = {.major = 1, .revision = 0},
      .protocol = std::move(protocol),
      .codec = {.value = "fcl.raw"},
      .max_inflight = 64,
      .max_frame_size = 16 * 1024 * 1024,
      .methods = {fcl::plugins::p2p_api_resolver::method{
         .name = "ping",
         .kind = fcl::api::method_kind::unary,
      }},
   };
}

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
         fcl::p2p::protocol_id{.value = "/fcl/test/blob-transfer/1"},
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
      p2p->publish_protocol(fcl::p2p::protocol_id{.value = "/fcl/test/duplicate/1"}, handler);
      p2p->publish_protocol(fcl::p2p::protocol_id{.value = "/fcl/test/duplicate/1"}, handler);
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_route_publisher_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "resolver-route-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto resolver = context.apis().get<fcl::plugins::p2p_api_resolver::api>(
         {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});

      auto plan = fcl::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class duplicate_resolver_route_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "duplicate-resolver-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto resolver = context.apis().get<fcl::plugins::p2p_api_resolver::api>(
         {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
      auto plan = fcl::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(plan, fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"});
      resolver->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/node-test-duplicate/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_custom_transport_route_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "resolver-custom-transport-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto resolver = context.apis().get<fcl::plugins::p2p_api_resolver::api>(
         {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
      auto plan = fcl::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/node-test-custom/1"},
                            fcl::plugins::p2p_api_resolver::publish_options{
                               .transport = fcl::api::transport::options{
                                  .codec = {.value = "fcl.test.raw"},
                                  .max_inflight = 7,
                                  .max_frame_size = 512 * 1024,
                               },
                            });
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_protocol_conflict_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "resolver-protocol-conflict"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p_node::api>(
         {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
      p2p->publish_protocol(
         fcl::p2p::protocol_id{.value = "/fcl/api/resolver/1"},
         [](fcl::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
            co_return;
         });
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class scripted_resolver_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return fcl::app::plugin_id{.value = "scripted-resolver"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p_node::api>(
         {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
      auto plan = fcl::api::binding()
                     .serve(context.apis())
                     .export_api<scripted_resolver_api>(
                        {.id = {"fcl.plugins.p2p_api_resolver.protocol"}, .major = 1, .min_revision = 0})
                     .build();
      p2p->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/resolver/1"});
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

class diagnostics_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_diagnostics::descriptor());
   }
};

class pubsub_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_pubsub::descriptor());
   }
};

class resolver_plugin_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_api_resolver::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "resolver-route-publisher"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_api_resolver"}},
         .factory = [] {
            return std::make_unique<resolver_route_publisher_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      co_return;
   }
};

class resolver_only_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_api_resolver::descriptor());
   }
};

class duplicate_resolver_plugin_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_api_resolver::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "duplicate-resolver-route"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_api_resolver"}},
         .factory = [] {
            return std::make_unique<duplicate_resolver_route_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      co_return;
   }
};

class resolver_custom_transport_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::plugins::p2p_api_resolver::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "resolver-custom-transport-route"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_api_resolver"}},
         .factory = [] {
            return std::make_unique<resolver_custom_transport_route_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      co_return;
   }
};

class resolver_protocol_conflict_application final : public fcl::app::application_shell {
 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "resolver-protocol-conflict"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
         .factory = [] {
            return std::make_unique<resolver_protocol_conflict_plugin>();
         },
      });
      registry.register_plugin(fcl::plugins::p2p_api_resolver::descriptor());
   }
};

class scripted_resolver_application final : public fcl::app::application_shell {
 public:
   explicit scripted_resolver_application(std::shared_ptr<scripted_resolver_state> state) : state_{std::move(state)} {}

 protected:
   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::plugins::p2p_node::descriptor());
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{.value = "scripted-resolver"},
         .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
         .factory = [] {
            return std::make_unique<scripted_resolver_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.apis().install<scripted_resolver_api>(scripted_resolver_api::describe(),
                                                    std::make_shared<scripted_resolver_api_impl>(state_));
      co_return;
   }

 private:
   std::shared_ptr<scripted_resolver_state> state_;
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

template <typename T>
concept has_metrics = requires(T& value) {
   value.metrics();
};

template <typename T>
concept has_peers = requires(T& value) {
   value.peers();
};

template <typename T>
concept has_pubsub_publish = requires(T& value) {
   value.publish(fcl::p2p::pubsub::topic{.value = "topic"}, std::vector<std::uint8_t>{},
                 fcl::plugins::p2p_pubsub::publish_options{});
};

template <typename T>
concept has_pubsub_subscribe = requires(T& value, fcl::plugins::p2p_pubsub::handler handler) {
   value.subscribe(fcl::p2p::pubsub::topic{.value = "topic"}, std::move(handler),
                   fcl::plugins::p2p_pubsub::subscribe_options{});
};

static_assert(!has_metrics<fcl::plugins::p2p_node::api>);
static_assert(!has_peers<fcl::plugins::p2p_node::api>);
static_assert(!has_pubsub_publish<fcl::plugins::p2p_node::api>);
static_assert(!has_pubsub_subscribe<fcl::plugins::p2p_node::api>);

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

BOOST_AUTO_TEST_CASE(p2p_diagnostics_plugin_config_is_described_from_public_schema) {
   auto plugin = fcl::plugins::p2p_diagnostics{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "p2p-diagnostics");

   const auto& max_peers = require_field(*descriptor, "max-peers");
   BOOST_TEST(max_peers.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_peers.default_value.storage) > 0U);

   const auto& max_sessions = require_field(*descriptor, "max-sessions");
   BOOST_TEST(max_sessions.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_sessions.default_value.storage) > 0U);
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_api_rejects_facade_calls_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto plugin = fcl::plugins::p2p_diagnostics{};
   auto apis = fcl::api::registry{};
   auto provider = fcl::api::installer{apis};
   fcl::asio::blocking::run(runtime, plugin.provide(provider));

   auto diagnostics = apis.get<fcl::plugins::p2p_diagnostics::api>(
      {.id = {"fcl.plugins.p2p_diagnostics"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)diagnostics->snapshot(), fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->network(), fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->resources(), fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->pubsub(), fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->peers(), fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->peer(test_peer(90)),
                     fcl::plugins::p2p_diagnostics::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_plugin_reports_live_p2p_node_state) {
   const auto server_peer = test_peer(91);
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

   const auto client_peer = test_peer(92);
   auto client_config = test_p2p_config(client_peer);
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});

   auto client = diagnostics_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto client_p2p = client.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   auto diagnostics = client.apis().get<fcl::plugins::p2p_diagnostics::api>(
      {.id = {"fcl.plugins.p2p_diagnostics"}, .major = 1, .min_revision = 0});

   auto remote = fcl::asio::blocking::run(
      client.runtime(),
      client_p2p->remote<node_test_api>(server_p2p->local_peer(),
                                        fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"}));
   const auto response = fcl::asio::blocking::run(
      client.runtime(),
      remote.call<int, int>({.id = {"node.test"}, .major = 1, .min_revision = 0}, "ping", 10));
   BOOST_TEST(response == 11);

   const auto snapshot = diagnostics->snapshot();
   BOOST_TEST(snapshot.network.local_peer.to_string() == client_p2p->local_peer().to_string());
   BOOST_TEST(snapshot.metrics.active_sessions >= 1U);
   BOOST_TEST(snapshot.resources.active_outbound_sessions >= 1U);
   BOOST_REQUIRE(!snapshot.sessions.empty());
   BOOST_TEST(snapshot.sessions.front().remote_peer.to_string() == server_peer.to_string());
   BOOST_REQUIRE(!diagnostics->peers().empty());

   const auto server_record = diagnostics->peer(server_peer);
   BOOST_TEST(server_record.peer.to_string() == server_peer.to_string());
   BOOST_CHECK_THROW((void)diagnostics->peer(test_peer(93)), fcl::plugins::p2p_diagnostics::exceptions::not_found);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_config_is_described_from_public_schema) {
   auto plugin = fcl::plugins::p2p_pubsub{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "p2p-pubsub");

   BOOST_TEST(require_field(*descriptor, "max-topics").has_default);
   BOOST_TEST(require_field(*descriptor, "max-handlers-per-topic").has_default);
   BOOST_TEST(require_field(*descriptor, "max-active-handlers").has_default);
   BOOST_TEST(require_field(*descriptor, "max-message-size").has_default);
   BOOST_TEST(require_field(*descriptor, "handler-deadline-ms").has_default);
   BOOST_TEST(require_field(*descriptor, "allowed-topics").has_default);
   BOOST_TEST(require_field(*descriptor, "denied-topics").has_default);
   BOOST_TEST(require_field(*descriptor, "sign-publishes").has_default);
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_api_rejects_facade_calls_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto plugin = fcl::plugins::p2p_pubsub{};
   auto apis = fcl::api::registry{};
   auto provider = fcl::api::installer{apis};
   fcl::asio::blocking::run(runtime, plugin.provide(provider));

   auto pubsub = apis.get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)pubsub->snapshot(), fcl::plugins::p2p_pubsub::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)pubsub->subscriptions(), fcl::plugins::p2p_pubsub::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(
                        runtime, pubsub->publish(fcl::p2p::pubsub::topic{.value = "fcl.before-init"}, {1, 2, 3})),
                     fcl::plugins::p2p_pubsub::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config();
      config.set("p2p-pubsub.max-topics", std::uint64_t{0});
      auto app = pubsub_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_pubsub::exceptions::invalid_config);
   }
   {
      auto config = test_p2p_config();
      config.set("p2p-pubsub.handler-deadline-ms", std::uint64_t{0});
      auto app = pubsub_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_pubsub::exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_requests_core_pubsub_capability_before_startup) {
   auto config = test_p2p_config(test_peer(94));
   config.set("p2p.listen", fcl::config::value::array_type{
                                fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   config.set("p2p-pubsub.sign-publishes", false);

   auto app = pubsub_application{};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});
   auto subscription = fcl::asio::blocking::run(
      app.runtime(),
      pubsub->subscribe(
         fcl::p2p::pubsub::topic{.value = "fcl.local"},
         [](fcl::plugins::p2p_pubsub::message) -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            co_return fcl::p2p::pubsub::validation_result::accept;
         }));

   BOOST_TEST(subscription.id != 0U);
   BOOST_TEST(pubsub->snapshot().core.topics == 1U);
   BOOST_TEST(pubsub->subscriptions().size() == 1U);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_publishes_and_subscribes_raw_and_typed_messages) {
   const auto subscriber_peer = test_peer(95);
   auto subscriber_config = test_p2p_config(subscriber_peer);
   subscriber_config.set("p2p.listen", fcl::config::value::array_type{
                                           fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   subscriber_config.set("p2p-pubsub.sign-publishes", false);

   auto subscriber = pubsub_application{};
   subscriber.configure(subscriber_config);
   fcl::asio::blocking::run(subscriber.runtime(), subscriber.startup());

   auto subscriber_p2p = subscriber.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto subscriber_endpoint = subscriber_p2p->local_endpoint();
   BOOST_REQUIRE(subscriber_endpoint.has_value());

   const auto publisher_peer = test_peer(96);
   auto publisher_config = test_p2p_config(publisher_peer);
   publisher_config.set("p2p.bootstrap",
                        fcl::config::value::array_type{fcl::config::value{subscriber_endpoint->to_string()}});
   publisher_config.set("p2p-pubsub.sign-publishes", false);

   auto publisher = pubsub_application{};
   publisher.configure(publisher_config);
   fcl::asio::blocking::run(publisher.runtime(), publisher.startup());

   auto received = std::make_shared<received_pubsub_messages>();
   auto subscriber_pubsub = subscriber.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});
   auto publisher_pubsub = publisher.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});

   const auto raw_topic = fcl::p2p::pubsub::topic{.value = "fcl.plugins.raw"};
   const auto typed_topic = fcl::p2p::pubsub::topic{.value = "fcl.plugins.typed"};
   auto first = fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         raw_topic, [received](fcl::plugins::p2p_pubsub::message message) mutable
                       -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message), fcl::p2p::pubsub::validation_result::accept);
            co_return fcl::p2p::pubsub::validation_result::accept;
         }));
   auto second = fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         raw_topic, [received](fcl::plugins::p2p_pubsub::message message) mutable
                       -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message), fcl::p2p::pubsub::validation_result::accept);
            co_return fcl::p2p::pubsub::validation_result::accept;
         }));
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe<pubsub_payload>(
         typed_topic, [received](fcl::plugins::p2p_pubsub::typed_message<pubsub_payload> message) mutable
                         -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message));
            co_return fcl::p2p::pubsub::validation_result::accept;
         }));

   BOOST_TEST(first.subject.value == raw_topic.value);
   BOOST_TEST(second.id != first.id);
   BOOST_REQUIRE_MESSAGE(wait_for_pubsub_peer(*publisher_pubsub.shared(), std::chrono::seconds{5}),
                         "publisher did not learn a remote PubSub topic subscription");

   (void)fcl::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(raw_topic, std::vector<std::uint8_t>{1, 2, 3, 4}));
   (void)fcl::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(typed_topic, pubsub_payload{.text = "hello", .value = 7}));

   if (!wait_for_count(*received, 2, 1)) {
      const auto publisher_snapshot = publisher_pubsub->snapshot();
      const auto subscriber_snapshot = subscriber_pubsub->snapshot();
      BOOST_FAIL("pubsub plugin delivery did not finish; raw="
                 << received->raw_size() << " typed=" << received->typed_size()
                 << " publisher_core_peers=" << publisher_snapshot.core.peers
                 << " publisher_published=" << publisher_snapshot.core.messages_published
                 << " subscriber_core_received=" << subscriber_snapshot.core.messages_received
                 << " subscriber_core_delivered=" << subscriber_snapshot.core.messages_delivered
                 << " subscriber_core_invalid=" << subscriber_snapshot.core.invalid_messages
                 << " subscriber_plugin_delivered=" << subscriber_snapshot.messages_delivered
                 << " subscriber_plugin_failures=" << subscriber_snapshot.handler_failures
                 << " subscriber_plugin_dropped=" << subscriber_snapshot.messages_dropped);
   }
   {
      auto lock = std::scoped_lock{received->mutex};
      BOOST_TEST(received->raw.size() == 2U);
      BOOST_TEST(fcl::p2p::valid_peer_id(received->raw.front().source));
      BOOST_TEST(received->raw.front().data == (std::vector<std::uint8_t>{1, 2, 3, 4}),
                 boost::test_tools::per_element());
      BOOST_TEST(received->typed.front().source.to_string() == received->raw.front().source.to_string());
      BOOST_TEST(received->typed.front().value.text == "hello");
      BOOST_TEST(received->typed.front().value.value == 7U);
   }

   fcl::asio::blocking::run(subscriber.runtime(), subscriber_pubsub->unsubscribe(first));
   BOOST_TEST(subscriber_pubsub->subscriptions().size() == 2U);
   fcl::asio::blocking::run(subscriber.runtime(), subscriber_pubsub->unsubscribe(second));
   BOOST_TEST(subscriber_pubsub->subscriptions().size() == 1U);

   fcl::asio::blocking::run(publisher.runtime(), publisher.shutdown());
   fcl::asio::blocking::run(subscriber.runtime(), subscriber.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_aggregates_handler_results_and_deadlines) {
   const auto subscriber_peer = test_peer(98);
   auto subscriber_config = test_p2p_config(subscriber_peer);
   subscriber_config.set("p2p.listen", fcl::config::value::array_type{
                                           fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   subscriber_config.set("p2p-pubsub.sign-publishes", false);

   auto subscriber = pubsub_application{};
   subscriber.configure(subscriber_config);
   fcl::asio::blocking::run(subscriber.runtime(), subscriber.startup());

   auto subscriber_p2p = subscriber.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto subscriber_endpoint = subscriber_p2p->local_endpoint();
   BOOST_REQUIRE(subscriber_endpoint.has_value());

   auto publisher_config = test_p2p_config(test_peer(99));
   publisher_config.set("p2p.bootstrap",
                        fcl::config::value::array_type{fcl::config::value{subscriber_endpoint->to_string()}});
   publisher_config.set("p2p-pubsub.sign-publishes", false);

   auto publisher = pubsub_application{};
   publisher.configure(publisher_config);
   fcl::asio::blocking::run(publisher.runtime(), publisher.startup());

   auto received = std::make_shared<received_pubsub_messages>();
   auto subscriber_pubsub = subscriber.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});
   auto publisher_pubsub = publisher.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});

   const auto aggregate_topic = fcl::p2p::pubsub::topic{.value = "fcl.plugins.aggregate"};
   const auto timeout_topic = fcl::p2p::pubsub::topic{.value = "fcl.plugins.timeout"};
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](fcl::plugins::p2p_pubsub::message message) mutable
                             -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message), fcl::p2p::pubsub::validation_result::ignore);
            co_return fcl::p2p::pubsub::validation_result::ignore;
         }));
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [](fcl::plugins::p2p_pubsub::message)
                             -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            throw fcl::plugins::p2p_pubsub::exceptions::handler_limit{"test handler failure"};
         }));
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](fcl::plugins::p2p_pubsub::message message) mutable
                             -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message), fcl::p2p::pubsub::validation_result::accept);
            co_return fcl::p2p::pubsub::validation_result::accept;
         }));
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](fcl::plugins::p2p_pubsub::message message) mutable
                             -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            received->push(std::move(message), fcl::p2p::pubsub::validation_result::reject);
            co_return fcl::p2p::pubsub::validation_result::reject;
         }));
   (void)fcl::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         timeout_topic,
         [](fcl::plugins::p2p_pubsub::message) -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
            auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
            timer.expires_after(std::chrono::milliseconds{100});
            co_await timer.async_wait(boost::asio::use_awaitable);
            co_return fcl::p2p::pubsub::validation_result::accept;
         },
         fcl::plugins::p2p_pubsub::subscribe_options{.handler_deadline = std::chrono::milliseconds{10}}));

   BOOST_REQUIRE_MESSAGE(wait_for_pubsub_peer(*publisher_pubsub.shared(), std::chrono::seconds{5}),
                         "publisher did not learn a remote PubSub topic subscription");

   (void)fcl::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(aggregate_topic, std::vector<std::uint8_t>{9}));
   BOOST_REQUIRE_MESSAGE(
      wait_for_pubsub_snapshot(
         *subscriber_pubsub.shared(),
         [](const fcl::plugins::p2p_pubsub::snapshot& snapshot) {
            return snapshot.messages_rejected >= 1 && snapshot.handler_failures >= 1;
         },
         std::chrono::seconds{5}),
      "PubSub handler aggregation did not finish");
   {
      auto lock = std::scoped_lock{received->mutex};
      BOOST_TEST(received->ignored == 1U);
      BOOST_TEST(received->accepted == 1U);
      BOOST_TEST(received->rejected == 1U);
   }

   (void)fcl::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(timeout_topic, std::vector<std::uint8_t>{10}));
   BOOST_REQUIRE_MESSAGE(
      wait_for_pubsub_snapshot(
         *subscriber_pubsub.shared(),
         [](const fcl::plugins::p2p_pubsub::snapshot& snapshot) {
            return snapshot.messages_ignored >= 1 && snapshot.handler_failures >= 2;
         },
         std::chrono::seconds{5}),
      "PubSub handler timeout did not finish");
   BOOST_TEST(subscriber_pubsub->snapshot().active_handlers == 0U);

   fcl::asio::blocking::run(publisher.runtime(), publisher.shutdown());
   fcl::asio::blocking::run(subscriber.runtime(), subscriber.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_enforces_topic_policy_and_handler_bounds) {
   auto config = test_p2p_config(test_peer(97));
   config.set("p2p-pubsub.sign-publishes", false);
   config.set("p2p-pubsub.max-handlers-per-topic", std::uint64_t{1});
   config.set("p2p-pubsub.max-message-size", std::uint64_t{4});
   config.set("p2p-pubsub.allowed-topics",
              fcl::config::value::array_type{fcl::config::value{"fcl.allowed"}});
   config.set("p2p-pubsub.denied-topics", fcl::config::value::array_type{fcl::config::value{"fcl.denied"}});

   auto app = pubsub_application{};
   app.configure(config);
   fcl::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<fcl::plugins::p2p_pubsub::api>(
      {.id = {"fcl.plugins.p2p_pubsub"}, .major = 1, .min_revision = 0});
   auto handler = [](fcl::plugins::p2p_pubsub::message) -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
      co_return fcl::p2p::pubsub::validation_result::ignore;
   };

   auto subscription = fcl::asio::blocking::run(
      app.runtime(), pubsub->subscribe(fcl::p2p::pubsub::topic{.value = "fcl.allowed"}, handler));
   BOOST_TEST(subscription.id != 0U);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(),
                                              pubsub->subscribe(fcl::p2p::pubsub::topic{.value = "fcl.allowed"},
                                                                handler)),
                     fcl::plugins::p2p_pubsub::exceptions::handler_limit);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(),
                                              pubsub->publish(fcl::p2p::pubsub::topic{.value = "fcl.denied"}, {1})),
                     fcl::plugins::p2p_pubsub::exceptions::topic_not_allowed);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(),
                                              pubsub->publish(fcl::p2p::pubsub::topic{.value = "fcl.allowed"},
                                                              {1, 2, 3, 4, 5})),
                     fcl::plugins::p2p_pubsub::exceptions::message_too_large);

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_plugin_config_is_described_from_public_schema) {
   auto plugin = fcl::plugins::p2p_api_resolver{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "p2p-api-resolver");

   const auto& protocol = require_field(*descriptor, "protocol-id");
   BOOST_TEST(protocol.has_default);
   BOOST_TEST(std::get<std::string>(protocol.default_value.storage) == "/fcl/api/resolver/1");

   const auto& cache_ttl = require_field(*descriptor, "cache-ttl-ms");
   BOOST_TEST(cache_ttl.has_default);
   BOOST_TEST(std::get<std::uint64_t>(cache_ttl.default_value.storage) > 0U);

   const auto& max_peers = require_field(*descriptor, "max-cached-peers");
   BOOST_TEST(max_peers.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_peers.default_value.storage) > 0U);

   const auto& max_apis = require_field(*descriptor, "max-apis-per-peer");
   BOOST_TEST(max_apis.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_apis.default_value.storage) > 0U);
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_facade_calls_before_initialize) {
   auto runtime = fcl::asio::runtime{};
   auto plugin = fcl::plugins::p2p_api_resolver{};
   auto apis = fcl::api::registry{};
   auto provider = fcl::api::installer{apis};
   fcl::asio::blocking::run(runtime, plugin.provide(provider));

   auto resolver = apis.get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});

   auto plan = fcl::api::binding().serve(apis).build();
   BOOST_CHECK_THROW(resolver->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/fcl/api/node-test/1"}),
                     fcl::plugins::p2p_api_resolver::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)resolver->local_apis(),
                     fcl::plugins::p2p_api_resolver::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime, resolver->peer_apis(test_peer(40))),
                     fcl::plugins::p2p_api_resolver::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config(test_peer(45));
      config.set("p2p-api-resolver.protocol-id", std::string{"fcl/api/resolver/1"});
      auto app = resolver_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_api_resolver::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config(test_peer(46));
      config.set("p2p-api-resolver.max-cached-peers", std::uint64_t{0});
      auto app = resolver_only_application{};
      BOOST_CHECK_THROW(app.configure(config), fcl::plugins::p2p_api_resolver::exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_publishes_metadata_and_delegates_route_mounting) {
   auto app = resolver_plugin_application{};
   app.configure(test_p2p_config(test_peer(50)));
   fcl::asio::blocking::run(app.runtime(), app.initialize());

   auto resolver = app.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   const auto entries = resolver->local_apis();
   BOOST_REQUIRE_EQUAL(entries.size(), 1U);
   BOOST_TEST(entries.front().id.value == "node.test");
   BOOST_TEST(entries.front().version.major == 1U);
   BOOST_TEST(entries.front().version.revision == 0U);
   BOOST_TEST(entries.front().protocol == "/fcl/api/node-test/1");
   BOOST_TEST(entries.front().codec.value == "fcl.raw");
   BOOST_REQUIRE_EQUAL(entries.front().methods.size(), 1U);
   BOOST_TEST(entries.front().methods.front().name == "ping");
   BOOST_TEST(static_cast<int>(entries.front().methods.front().kind) ==
              static_cast<int>(fcl::api::method_kind::unary));

   fcl::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_duplicate_api_and_resolver_protocol_conflict) {
   {
      auto app = duplicate_resolver_plugin_application{};
      app.configure(test_p2p_config(test_peer(60)));
      BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.initialize()),
                        fcl::plugins::p2p_api_resolver::exceptions::duplicate_api);
   }

   {
      auto app = resolver_protocol_conflict_application{};
      app.configure(test_p2p_config(test_peer(61)));
      BOOST_CHECK_THROW(fcl::asio::blocking::run(app.runtime(), app.initialize()),
                        fcl::plugins::p2p_api_resolver::exceptions::duplicate_api);
   }
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_resolves_remote_api_and_opens_typed_remote) {
   const auto server_peer = test_peer(70);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(71));
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   const auto remote_entries = fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(remote_entries.size(), 1U);
   BOOST_TEST(remote_entries.front().protocol == "/fcl/api/node-test/1");

   auto resolved = fcl::asio::blocking::run(
      client.runtime(), resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 0}));
   BOOST_TEST(resolved.api.protocol == "/fcl/api/node-test/1");

   auto remote = fcl::asio::blocking::run(client.runtime(), resolver->remote<node_test_api>(server_peer));
   const auto response = fcl::asio::blocking::run(
      client.runtime(),
      remote.call<int, int>({.id = {"node.test"}, .major = 1, .min_revision = 0}, "ping", 41));
   BOOST_TEST(response == 42);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_remote_honors_advertised_transport_options) {
   const auto server_peer = test_peer(72);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_custom_transport_application{};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(73));
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   const auto remote_entries = fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(remote_entries.size(), 1U);
   BOOST_TEST(remote_entries.front().codec.value == "fcl.test.raw");
   BOOST_TEST(remote_entries.front().max_inflight == 7U);
   BOOST_TEST(remote_entries.front().max_frame_size == 512U * 1024U);

   auto remote = fcl::asio::blocking::run(client.runtime(), resolver->remote<node_test_api>(server_peer));
   const auto response = fcl::asio::blocking::run(
      client.runtime(),
      remote.call<int, int>({.id = {"node.test"}, .major = 1, .min_revision = 0}, "ping", 41));
   BOOST_TEST(response == 42);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_enforces_version_compatibility) {
   const auto server_peer = test_peer(80);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(81));
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   BOOST_CHECK_NO_THROW(fcl::asio::blocking::run(
      client.runtime(), resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 0})));
   BOOST_CHECK_THROW(fcl::asio::blocking::run(
                        client.runtime(),
                        resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 10})),
                     fcl::plugins::p2p_api_resolver::exceptions::incompatible_api);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_malformed_remote_metadata_without_caching_it) {
   const auto server_peer = test_peer(85);
   auto bad = resolver_test_entry("/fcl/api/node-test/1");
   auto duplicate = fcl::plugins::p2p_api_resolver::response{.apis = {bad, bad}};
   auto good = fcl::plugins::p2p_api_resolver::response{
      .apis = {resolver_test_entry("/fcl/api/node-test/1")},
   };
   auto state = std::make_shared<scripted_resolver_state>(
      scripted_resolver_state{.responses = {std::move(duplicate), std::move(good)}});

   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = scripted_resolver_application{state};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(86));
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   BOOST_CHECK_THROW(fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)),
                     fcl::plugins::p2p_api_resolver::exceptions::protocol_error);
   const auto entries = fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(entries.size(), 1U);
   BOOST_TEST(state->calls == 2U);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
   fcl::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_cache_ttl_and_force_refresh_are_behavioral) {
   const auto server_peer = test_peer(90);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("p2p.listen", fcl::config::value::array_type{
                                      fcl::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   fcl::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<fcl::plugins::p2p_node::api>(
      {.id = {"fcl.plugins.p2p_node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(91));
   client_config.set("p2p.bootstrap",
                     fcl::config::value::array_type{fcl::config::value{server_endpoint->to_string()}});
   client_config.set("p2p-api-resolver.cache-ttl-ms", std::uint64_t{200});
   auto client = resolver_only_application{};
   client.configure(client_config);
   fcl::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<fcl::plugins::p2p_api_resolver::api>(
      {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1, .min_revision = 0});
   BOOST_REQUIRE_EQUAL(fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)).size(), 1U);

   fcl::asio::blocking::run(server.runtime(), server.shutdown());
   BOOST_REQUIRE_EQUAL(fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)).size(), 1U);

   BOOST_CHECK_THROW(fcl::asio::blocking::run(client.runtime(),
                                              resolver->peer_apis(server_peer, {.force_refresh = true})),
                     fcl::exceptions::base);

   std::this_thread::sleep_for(std::chrono::milliseconds{250});
   BOOST_CHECK_THROW(fcl::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)),
                     fcl::exceptions::base);

   fcl::asio::blocking::run(client.runtime(), client.shutdown());
}
