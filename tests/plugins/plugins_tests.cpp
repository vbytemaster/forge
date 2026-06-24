#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/api/macros.hpp>
#include <forge/exceptions/macros.hpp>
#include <forge/http_api/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.transport.api.exceptions;
import forge.transport.api.options;
import forge.transport.api.client;
import forge.transport.api.connection;
import forge.transport.api.server;
import forge.app.exceptions;
import forge.app.application;
import forge.app.events;
import forge.app.diagnostics;
import forge.app.signals;
import forge.app.views;
import forge.app.plugin_context;
import forge.app.plugin;
import forge.app.plugin_registry;
import forge.app.application_shell;
import forge.app.application_builder;
import forge.app.runner;
import forge.app.daemon;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.config.component;
import forge.config.document;
import forge.config.value;
import forge.config.decode;
import forge.crypto.asymmetric;
import forge.crypto.ed25519;
import forge.crypto.p256;
import forge.crypto.secp256k1;
import forge.crypto.sha256;
import forge.env;
import forge.http.api.binding;
import forge.http.base_url;
import forge.http.api.parameters;
import forge.http.body;
import forge.http.client;
import forge.http.connection;
import forge.http.exceptions;
import forge.http.file;
import forge.http.api.mapping;
import forge.http.middleware;
import forge.http.api.proxy;
import forge.http.router;
import forge.http.stream;
import forge.http.types;
import forge.http.upload;
import forge.p2p.exceptions;
import forge.p2p.identity;
import forge.p2p.endpoint;
import forge.p2p.envelope;
import forge.p2p.identify;
import forge.p2p.diagnostics;
import forge.p2p.discovery;
import forge.p2p.dht;
import forge.p2p.rendezvous;
import forge.p2p.pubsub;
import forge.p2p.reachability;
import forge.p2p.hole_punch;
import forge.p2p.protocol;
import forge.p2p.message;
import forge.p2p.scoring;
import forge.p2p.relay;
import forge.p2p.resource_manager;
import forge.p2p.stream;
import forge.p2p.negotiation;
import forge.p2p.peer_store;
import forge.p2p.node;
import forge.p2p.api;
import forge.plugins.crypto.signer.types;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.plugin;
import forge.plugins.p2p.diagnostics.types;
import forge.plugins.p2p.diagnostics.exceptions;
import forge.plugins.p2p.diagnostics.api;
import forge.plugins.p2p.diagnostics.plugin;
import forge.plugins.p2p.pubsub.types;
import forge.plugins.p2p.pubsub.exceptions;
import forge.plugins.p2p.pubsub.api;
import forge.plugins.p2p.pubsub.plugin;
import forge.plugins.p2p.resolver.types;
import forge.plugins.p2p.resolver.exceptions;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.plugin;
import forge.plugins.p2p.node.types;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.plugin;
import forge.plugins.http.server.types;
import forge.plugins.http.server.exceptions;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.api;
import forge.plugins.http.server.plugin;
import forge.program_options;
import forge.raw.raw;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

template <typename T>
concept accepts_raw_http_binding = requires(T& api, forge::http::api::binding_plan binding) {
   api.publish(std::move(binding), forge::plugins::http::server::publish_options{});
};

namespace raw_http = forge::http;
using raw_http_middleware = raw_http::middleware_descriptor;

template <typename T>
concept accepts_raw_http_middleware = requires(T& api, raw_http_middleware descriptor) {
   api.use(std::move(descriptor));
};

static_assert(!accepts_raw_http_binding<forge::plugins::http::server::api>);
static_assert(!accepts_raw_http_middleware<forge::plugins::http::server::api>);

[[nodiscard]] bool has_internal_forge_header(const forge::http::response& value) {
   for (const auto& header : value.headers()) {
      if (header.name.starts_with("X-FORGE-")) {
         return true;
      }
   }
   return false;
}

struct pubsub_payload {
   std::string text;
   std::uint32_t value = 0;

   bool operator==(const pubsub_payload&) const = default;
};
BOOST_DESCRIBE_STRUCT(pubsub_payload, (), (text, value))

struct operation_request {
   std::string request_id;
   std::string subject;
   std::uint64_t revision = 0;

   bool operator==(const operation_request&) const = default;
};
BOOST_DESCRIBE_STRUCT(operation_request, (), (request_id, subject, revision))

struct operation_receipt {
   std::string request_id;
   bool accepted = false;
   std::uint64_t applied_revision = 0;
   std::string authority;
   std::string evidence;

   bool operator==(const operation_receipt&) const = default;
};
BOOST_DESCRIBE_STRUCT(operation_receipt, (), (request_id, accepted, applied_revision, authority, evidence))

struct http_read_request {
   std::string ref;
   std::uint32_t offset = 0;
   std::uint32_t limit = 0;
};

struct http_write_request {
   std::string ref;
   std::string bytes;
};

struct http_chunk {
   std::string bytes;
};

struct http_stream_read_request {
   std::string ref;
};

BOOST_DESCRIBE_STRUCT(http_read_request, (), (ref, offset, limit))
BOOST_DESCRIBE_STRUCT(http_write_request, (), (ref, bytes))
BOOST_DESCRIBE_STRUCT(http_chunk, (), (bytes))
BOOST_DESCRIBE_STRUCT(http_stream_read_request, (), (ref))

namespace plugin_test_contract {

class node_test_api
    : public forge::api::contract<node_test_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~node_test_api() = default;
   virtual boost::asio::awaitable<int> ping(int request) = 0;
};

class peer_context_test_api
    : public forge::api::contract<peer_context_test_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~peer_context_test_api() = default;
   virtual boost::asio::awaitable<std::string> remote_peer(std::string request) = 0;
};

class receipt_test_api
    : public forge::api::contract<receipt_test_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~receipt_test_api() = default;
   virtual boost::asio::awaitable<operation_receipt> apply(operation_request request) = 0;
};

class http_cache_api
    : public forge::api::contract<http_cache_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~http_cache_api() = default;
   virtual boost::asio::awaitable<http_chunk> read(http_read_request request) = 0;
   virtual boost::asio::awaitable<http_chunk> write(http_write_request request) = 0;
};

class http_stream_api
    : public forge::api::contract<http_stream_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~http_stream_api() = default;
   virtual boost::asio::awaitable<forge::http::streaming_response> download(http_stream_read_request request) = 0;
};

class http_empty_api
    : public forge::api::contract<http_empty_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~http_empty_api() = default;
   virtual boost::asio::awaitable<forge::http::empty_response> clear(http_stream_read_request request) = 0;
};

class scripted_resolver_api
    : public forge::api::contract<scripted_resolver_api, forge::api::surface::local | forge::api::surface::remote> {
 public:
   virtual ~scripted_resolver_api() = default;
   virtual boost::asio::awaitable<forge::plugins::p2p::resolver::response>
   query(forge::plugins::p2p::resolver::query request) = 0;
};

} // namespace plugin_test_contract

FORGE_API(::plugin_test_contract::node_test_api, FORGE_API_CONTRACT("node.test", 1, 0), FORGE_API_METHOD(ping))
FORGE_API(::plugin_test_contract::peer_context_test_api, FORGE_API_CONTRACT("peer-context.test", 1, 0),
        FORGE_API_METHOD(remote_peer))
FORGE_API(::plugin_test_contract::receipt_test_api, FORGE_API_CONTRACT("receipt.test", 1, 0), FORGE_API_METHOD(apply))
FORGE_API(::plugin_test_contract::http_cache_api, FORGE_API_CONTRACT("cache", 1, 0), FORGE_API_METHOD(read),
        FORGE_API_METHOD(write))
FORGE_API(::plugin_test_contract::http_stream_api, FORGE_API_CONTRACT("stream-cache", 1, 0),
        FORGE_API_METHOD_TYPED(download, ::http_stream_read_request, ::forge::http::streaming_response))
FORGE_API(::plugin_test_contract::http_empty_api, FORGE_API_CONTRACT("empty-cache", 1, 0),
        FORGE_API_METHOD_TYPED(clear, ::http_stream_read_request, ::forge::http::empty_response))
FORGE_API(::plugin_test_contract::scripted_resolver_api,
        FORGE_API_CONTRACT("forge.plugins.p2p.resolver.protocol", 1, 0), FORGE_API_METHOD(query))

FORGE_HTTP_API(::plugin_test_contract::http_cache_api,
             FORGE_HTTP_GET(read, "/cache/chunks/:ref?offset={offset}&limit={limit}"),
             FORGE_HTTP_PUT(write, "/cache/chunks/:ref", created))
FORGE_HTTP_API(::plugin_test_contract::http_stream_api,
             FORGE_HTTP_GET(download, "/stream/:ref", FORGE_HTTP_RESPONSE_STREAM))
FORGE_HTTP_API(::plugin_test_contract::http_empty_api,
             FORGE_HTTP_DELETE(clear, "/empty/:ref", no_content))

namespace {

using plugin_test_contract::node_test_api;
using plugin_test_contract::peer_context_test_api;
using plugin_test_contract::receipt_test_api;
using plugin_test_contract::scripted_resolver_api;
using plugin_test_contract::http_cache_api;
using plugin_test_contract::http_stream_api;
using plugin_test_contract::http_empty_api;

namespace crypto_signer = forge::plugins::crypto::signer;
namespace http_server = forge::plugins::http::server;

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

[[nodiscard]] forge::p2p::peer_id test_peer(std::uint8_t seed) {
   return forge::p2p::make_peer_id(
      {.type = forge::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, seed)});
}

[[nodiscard]] forge::config::document test_p2p_config(std::optional<forge::p2p::peer_id> peer = std::nullopt) {
   auto document = forge::config::document{};
   document.set("plugins.p2p.node.allow-insecure-test-mode", true);
   document.set("plugins.p2p.node.certificate-pem", std::string{test_certificate()});
   document.set("plugins.p2p.node.private-key-pem", std::string{test_private_key()});
   if (peer.has_value()) {
      document.set("plugins.p2p.node.peer-id", peer->to_string());
   }
   return document;
}

class node_test_api_impl final : public node_test_api {
 public:
   boost::asio::awaitable<int> ping(int request) override {
      co_return request + 1;
   }
};

class peer_context_test_api_impl final : public peer_context_test_api {
 public:
   boost::asio::awaitable<std::string> remote_peer(std::string request) override {
      co_return request;
   }
};

struct receipt_test_state {
   mutable std::mutex mutex;
   std::unordered_map<std::string, operation_receipt> receipts;
   std::size_t applied = 0;
};

class receipt_test_api_impl final : public receipt_test_api {
 public:
   explicit receipt_test_api_impl(std::shared_ptr<receipt_test_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<operation_receipt> apply(operation_request request) override {
      auto lock = std::scoped_lock{state_->mutex};
      if (const auto existing = state_->receipts.find(request.request_id); existing != state_->receipts.end()) {
         co_return existing->second;
      }

      const auto revision = ++state_->applied;
      auto receipt = operation_receipt{
         .request_id = request.request_id,
         .accepted = true,
         .applied_revision = revision,
         .authority = "receipt-test",
         .evidence = request.subject + ":" + std::to_string(request.revision) + ":" + std::to_string(revision),
      };
      auto [inserted, _] = state_->receipts.emplace(request.request_id, std::move(receipt));
      co_return inserted->second;
   }

 private:
   std::shared_ptr<receipt_test_state> state_;
};

class http_cache_api_impl final : public http_cache_api {
 public:
   boost::asio::awaitable<http_chunk> read(http_read_request request) override {
      co_return http_chunk{.bytes = request.ref + ":" + std::to_string(request.offset) + ":" +
                                    std::to_string(request.limit)};
   }

   boost::asio::awaitable<http_chunk> write(http_write_request request) override {
      co_return http_chunk{.bytes = request.ref + ":" + request.bytes};
   }
};

struct http_publish_state {
   std::string base_path;
   std::vector<std::string> middleware_events;
   bool short_circuit = false;
   bool replace_stream_after_next = false;
   bool empty_replace_stream_after_next = false;
   bool set_stream_content_type_after_next = false;
   std::atomic<unsigned> stream_calls = 0;
   std::atomic<unsigned> stream_chunks = 0;
};

class http_stream_api_impl final : public http_stream_api {
 public:
   explicit http_stream_api_impl(std::shared_ptr<http_publish_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<forge::http::streaming_response> download(http_stream_read_request request) override {
      state_->stream_calls.fetch_add(1);
      auto chunks = std::make_shared<std::vector<std::string>>(
         std::vector<std::string>{"stream:", request.ref, ":payload"});
      auto index = std::make_shared<std::size_t>(0);
      auto state = state_;
      co_return forge::http::streaming_response::from_source(
         forge::http::streaming_response_options{
            .content_type = "text/plain",
            .body =
               [chunks, index, state]() mutable -> boost::asio::awaitable<std::optional<forge::http::body_chunk>> {
                  if (*index == chunks->size()) {
                     co_return std::nullopt;
                  }
                  const auto& text = (*chunks)[(*index)++];
                  auto bytes = std::vector<std::byte>(text.size());
                  std::memcpy(bytes.data(), text.data(), text.size());
                  state->stream_chunks.fetch_add(1);
                  co_return forge::http::body_chunk{.bytes = std::move(bytes)};
               },
         });
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class http_empty_api_impl final : public http_empty_api {
 public:
   boost::asio::awaitable<forge::http::empty_response> clear(http_stream_read_request) override {
      co_return forge::http::empty_response{.status_code = forge::http::status::no_content};
   }
};

class http_cache_publisher_plugin final : public forge::app::plugin {
 public:
   explicit http_cache_publisher_plugin(std::shared_ptr<http_publish_state> state) : state_{std::move(state)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "http-cache-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<http_server::api>(http_server::api::ref());
      co_await http->publish<http_cache_api>(http_server::publish_options{.base_path = state_->base_path});
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class http_stream_publisher_plugin final : public forge::app::plugin {
 public:
   explicit http_stream_publisher_plugin(std::shared_ptr<http_publish_state> state) : state_{std::move(state)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "http-stream-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<http_server::api>(http_server::api::ref());
      co_await http->publish<http_stream_api>(http_server::publish_options{.base_path = state_->base_path});
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class http_empty_publisher_plugin final : public forge::app::plugin {
 public:
   explicit http_empty_publisher_plugin(std::shared_ptr<http_publish_state> state) : state_{std::move(state)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "http-empty-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<http_server::api>(http_server::api::ref());
      co_await http->publish<http_empty_api>(http_server::publish_options{.base_path = state_->base_path});
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class http_middleware_plugin final : public forge::app::plugin {
 public:
   explicit http_middleware_plugin(std::shared_ptr<http_publish_state> state) : state_{std::move(state)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "http-middleware"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<http_server::api>(http_server::api::ref());
      auto state = state_;
      co_await http->use(http_server::middleware_descriptor{
         .id = "security",
         .phase = http_server::middleware_phase::security,
         .order = 10,
         .path_prefix = "/api",
         .handler = [state](const http_server::middleware_request& request,
                            http_server::middleware_next next) -> boost::asio::awaitable<http_server::middleware_response> {
            state->middleware_events.push_back("security");
            if (state->short_circuit && !request.header("Authorization").has_value()) {
               co_return http_server::middleware_response::text(forge::http::status::unauthorized,
                                                               "missing authorization");
            }
            co_return co_await next();
         },
      });
      co_await http->use(http_server::middleware_descriptor{
         .id = "before",
         .phase = http_server::middleware_phase::before_handler,
         .order = 20,
         .path_prefix = "/api",
         .handler = [state](const http_server::middleware_request&,
                            http_server::middleware_next next) -> boost::asio::awaitable<http_server::middleware_response> {
            state->middleware_events.push_back("before");
            auto response = co_await next();
            if (state->replace_stream_after_next) {
               co_return http_server::middleware_response::text(forge::http::status::forbidden, "blocked");
            }
            if (state->empty_replace_stream_after_next) {
               response.set_status(forge::http::status::forbidden);
               response.clear_body();
               co_return response;
            }
            if (state->set_stream_content_type_after_next) {
               response.set_content_type("application/x-ndjson");
            }
            response.set_header("Server", "forge-test");
            co_return response;
         },
      });
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class duplicate_http_cache_publisher_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "duplicate-http-cache-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<http_server::api>(http_server::api::ref());
      co_await http->publish<http_cache_api>(http_server::publish_options{.base_path = "/api"});
      co_await http->publish<http_cache_api>(http_server::publish_options{.base_path = "/api"});
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class late_http_publish_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "late-http-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> startup() override {
      auto http = context_->apis().get<http_server::api>(http_server::api::ref());
      co_await http->publish<http_cache_api>(http_server::publish_options{.base_path = "/late"});
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      context_ = &context;
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   forge::app::plugin_context* context_ = nullptr;
};

class temp_directory {
 public:
   temp_directory() {
      path_ = std::filesystem::temp_directory_path() /
              ("forge-plugin-http-test-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::filesystem::create_directories(path_);
   }

   ~temp_directory() {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
   }

   [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
   }

   void write(std::string_view name, std::string_view bytes) const {
      auto output = std::ofstream{path_ / std::string{name}, std::ios::binary};
      output << bytes;
   }

 private:
   std::filesystem::path path_;
};

struct received_pubsub_messages {
   mutable std::mutex mutex;
   std::vector<forge::plugins::p2p::pubsub::message> raw;
   std::vector<forge::plugins::p2p::pubsub::typed_message<pubsub_payload>> typed;
   std::size_t accepted = 0;
   std::size_t rejected = 0;
   std::size_t ignored = 0;

   void push(forge::plugins::p2p::pubsub::message value, forge::p2p::pubsub::validation_result result) {
      auto lock = std::scoped_lock{mutex};
      raw.push_back(std::move(value));
      switch (result) {
      case forge::p2p::pubsub::validation_result::accept:
         ++accepted;
         break;
      case forge::p2p::pubsub::validation_result::reject:
         ++rejected;
         break;
      case forge::p2p::pubsub::validation_result::ignore:
         ++ignored;
         break;
      }
   }

   void push(forge::plugins::p2p::pubsub::typed_message<pubsub_payload> value) {
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
bool wait_for_pubsub_snapshot(const forge::plugins::p2p::pubsub::api& pubsub, Predicate predicate,
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

bool wait_for_pubsub_peer(const forge::plugins::p2p::pubsub::api& pubsub, std::chrono::milliseconds timeout) {
   return wait_for_pubsub_snapshot(
      pubsub,
      [](const forge::plugins::p2p::pubsub::snapshot& snapshot) {
         return snapshot.core.peers > 0;
      },
      timeout);
}

template <typename Predicate>
boost::asio::awaitable<bool> async_wait_for_condition(Predicate predicate, std::chrono::milliseconds timeout) {
   auto executor = co_await boost::asio::this_coro::executor;
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
         co_return true;
      }
      auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{10}};
      co_await timer.async_wait(boost::asio::use_awaitable);
   }
   co_return predicate();
}

struct fake_pubsub_source_state {
   mutable std::mutex mutex;
   bool release_join = false;
   bool fail_join = false;
   std::size_t enable_calls = 0;
   std::size_t join_attempts = 0;
   std::size_t leave_attempts = 0;
   std::size_t joined_handlers = 0;

   [[nodiscard]] std::size_t joins() const {
      auto lock = std::scoped_lock{mutex};
      return join_attempts;
   }

   [[nodiscard]] bool released() const {
      auto lock = std::scoped_lock{mutex};
      return release_join;
   }

   void release(bool fail) {
      auto lock = std::scoped_lock{mutex};
      fail_join = fail;
      release_join = true;
   }
};

struct subscribe_task_result {
   mutable std::mutex mutex;
   bool done = false;
   std::exception_ptr error;
   std::optional<forge::plugins::p2p::pubsub::subscription> value;

   void complete(forge::plugins::p2p::pubsub::subscription subscription) {
      auto lock = std::scoped_lock{mutex};
      value = std::move(subscription);
      done = true;
   }

   void fail(std::exception_ptr exception) {
      auto lock = std::scoped_lock{mutex};
      error = std::move(exception);
      done = true;
   }

   [[nodiscard]] bool finished() const {
      auto lock = std::scoped_lock{mutex};
      return done;
   }

   [[nodiscard]] bool failed() const {
      auto lock = std::scoped_lock{mutex};
      return error != nullptr;
   }
};

class fake_pubsub_source final : public forge::plugins::p2p::node::pubsub_source {
 public:
   explicit fake_pubsub_source(std::shared_ptr<fake_pubsub_source_state> state) : state_{std::move(state)} {}

   void enable(forge::p2p::pubsub::options) override {
      auto lock = std::scoped_lock{state_->mutex};
      ++state_->enable_calls;
   }

   [[nodiscard]] forge::p2p::peer_id local_peer() const override {
      return forge::p2p::peer_id{.value = "fake-pubsub-peer"};
   }

   boost::asio::awaitable<forge::p2p::pubsub::message>
   async_publish_message(forge::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                         forge::p2p::pubsub::publish_options) override {
      co_return forge::p2p::pubsub::message{
         .from = local_peer(),
         .data = std::move(data),
         .seqno = {1},
         .subject = std::move(subject),
      };
   }

   boost::asio::awaitable<forge::p2p::pubsub::subscription>
   async_join_topic(forge::p2p::pubsub::topic subject, forge::p2p::pubsub::handler) override {
      auto executor = co_await boost::asio::this_coro::executor;
      {
         auto lock = std::scoped_lock{state_->mutex};
         ++state_->join_attempts;
      }
      while (!state_->released()) {
         auto timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{10}};
         co_await timer.async_wait(boost::asio::use_awaitable);
      }
      {
         auto lock = std::scoped_lock{state_->mutex};
         if (state_->fail_join) {
            FORGE_THROW_EXCEPTION(forge::plugins::p2p::pubsub::exceptions::handler_limit,
                                "fake PubSub source join failed");
         }
         ++state_->joined_handlers;
      }
      co_return forge::p2p::pubsub::subscription{.subscribe = true, .subject = std::move(subject)};
   }

   boost::asio::awaitable<void> async_leave_topic(forge::p2p::pubsub::topic) override {
      auto lock = std::scoped_lock{state_->mutex};
      ++state_->leave_attempts;
      co_return;
   }

   forge::p2p::pubsub::snapshot snapshot() const override {
      return forge::p2p::pubsub::snapshot{};
   }

 private:
   std::shared_ptr<fake_pubsub_source_state> state_;
};

class fake_p2p_node_plugin final : public forge::app::plugin {
 public:
   explicit fake_p2p_node_plugin(std::shared_ptr<fake_pubsub_source_state> state) : state_{std::move(state)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "forge.plugins.p2p.node"};
   }

   [[nodiscard]] std::string version() const override {
      return "test";
   }

   boost::asio::awaitable<void> provide(forge::api::provider& provider) override {
      provider.install<forge::plugins::p2p::node::pubsub_source>(std::make_shared<fake_pubsub_source>(state_));
      co_return;
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context&) override {
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::shared_ptr<fake_pubsub_source_state> state_;
};

struct scripted_resolver_state {
   std::vector<forge::plugins::p2p::resolver::response> responses;
   std::size_t calls = 0;
};

class scripted_resolver_api_impl final : public scripted_resolver_api {
 public:
   explicit scripted_resolver_api_impl(std::shared_ptr<scripted_resolver_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<forge::plugins::p2p::resolver::response>
   query(forge::plugins::p2p::resolver::query) override {
      const auto index = std::min(state_->calls, state_->responses.size() - 1);
      ++state_->calls;
      co_return state_->responses[index];
   }

 private:
   std::shared_ptr<scripted_resolver_state> state_;
};

[[nodiscard]] forge::plugins::p2p::resolver::entry resolver_test_entry(std::string protocol) {
   return forge::plugins::p2p::resolver::entry{
      .id = {.value = "node.test"},
      .version = {.major = 1, .revision = 0},
      .protocol = std::move(protocol),
      .codec = {.value = "forge.raw"},
      .max_inflight = 64,
      .max_frame_size = 16 * 1024 * 1024,
      .methods = {forge::plugins::p2p::resolver::method{
         .name = "ping",
         .kind = forge::api::method_kind::unary,
      }},
   };
}

class route_publisher_plugin final : public forge::app::plugin {
 public:
   explicit route_publisher_plugin(plugin_log& log) : log_{&log} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "route-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto p2p = context.apis().get<forge::plugins::p2p::node::api>(
         {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});

      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .export_api<peer_context_test_api>({.id = {"peer-context.test"}, .major = 1, .min_revision = 0})
                     .interceptor(forge::api::interceptor()
                                     .id("peer-context")
                                     .phase(forge::api::interceptor_phase::authorize)
                                     .handler([](forge::api::call_context& value) -> boost::asio::awaitable<void> {
                                        if (value.api.id.value != "peer-context.test" ||
                                            value.method != "remote_peer") {
                                           co_return;
                                        }
                                        const auto peer =
                                           forge::api::metadata_value(value.meta,
                                                                    forge::api::p2p_remote_peer_metadata_key)
                                              .value_or("missing");
                                        const auto request = forge::raw::unpack<std::string>(value.payload);
                                        const auto response = peer + ":" + request;
                                        value.payload.clear();
                                        forge::raw::pack<std::string>(value.payload, response);
                                        co_return;
                                     })
                                     .build())
                     .build();
      p2p->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/node-test/1"});
      p2p->publish_protocol(
         forge::p2p::protocol_id{.value = "/forge/test/blob-transfer/1"},
         [](forge::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
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

class duplicate_route_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "duplicate-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto p2p = context.apis().get<forge::plugins::p2p::node::api>(
         {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
      auto handler = [](forge::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
         co_return;
      };
      p2p->publish_protocol(forge::p2p::protocol_id{.value = "/forge/test/duplicate/1"}, handler);
      p2p->publish_protocol(forge::p2p::protocol_id{.value = "/forge/test/duplicate/1"}, handler);
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_route_publisher_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "resolver-route-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
         {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});

      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/node-test/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class duplicate_resolver_route_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "duplicate-resolver-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
         {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(plan, forge::p2p::protocol_id{.value = "/forge/api/node-test/1"});
      resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/node-test-duplicate/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_custom_transport_route_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "resolver-custom-transport-route"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
         {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<node_test_api>({.id = {"node.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/node-test-custom/1"},
                            forge::plugins::p2p::resolver::publish_options{
                               .transport = forge::transport::api::options{
                                  .codec = {.value = "forge.test.raw"},
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

class receipt_route_publisher_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "receipt-route-publisher"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
         {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});

      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<receipt_test_api>({.id = {"receipt.test"}, .major = 1, .min_revision = 0})
                     .build();
      resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/receipt-test/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class resolver_protocol_conflict_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "resolver-protocol-conflict"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto p2p = context.apis().get<forge::plugins::p2p::node::api>(
         {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
      p2p->publish_protocol(
         forge::p2p::protocol_id{.value = "/forge/api/resolver/1"},
         [](forge::p2p::node::incoming_protocol_stream) -> boost::asio::awaitable<void> {
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

class scripted_resolver_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "scripted-resolver"};
   }

   [[nodiscard]] std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto p2p = context.apis().get<forge::plugins::p2p::node::api>(
         {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
      auto plan = forge::api::binding()
                     .serve(context.apis())
                     .export_api<scripted_resolver_api>(
                        {.id = {"forge.plugins.p2p.resolver.protocol"}, .major = 1, .min_revision = 0})
                     .build();
      p2p->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/resolver/1"});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class p2p_plugin_application final : public forge::app::application_shell {
 public:
   explicit p2p_plugin_application(plugin_log& log) : log_{&log} {}

   protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "route-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
         .factory = [this] {
            return std::make_unique<route_publisher_plugin>(*log_);
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      context.apis().install<peer_context_test_api>(peer_context_test_api::describe(),
                                                    std::make_shared<peer_context_test_api_impl>());
      co_return;
   }

 private:
   plugin_log* log_ = nullptr;
};

class duplicate_p2p_plugin_application final : public forge::app::application_shell {
   protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "duplicate-route"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
         .factory = [] {
            return std::make_unique<duplicate_route_plugin>();
         },
      });
   }
};

class p2p_only_application final : public forge::app::application_shell {
   protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
   }
};

class diagnostics_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::diagnostics::descriptor());
   }
};

class pubsub_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::pubsub::descriptor());
   }
};

class fake_pubsub_application final : public forge::app::application_shell {
 public:
   explicit fake_pubsub_application(std::shared_ptr<fake_pubsub_source_state> state) : state_{std::move(state)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "forge.plugins.p2p.node"},
         .factory = [state = state_] {
            return std::make_unique<fake_p2p_node_plugin>(state);
         },
      });
      registry.register_plugin(forge::plugins::p2p::pubsub::descriptor());
   }

 private:
   std::shared_ptr<fake_pubsub_source_state> state_;
};

class resolver_plugin_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "resolver-route-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
         .factory = [] {
            return std::make_unique<resolver_route_publisher_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      context.apis().install<peer_context_test_api>(peer_context_test_api::describe(),
                                                    std::make_shared<peer_context_test_api_impl>());
      co_return;
   }
};

class resolver_only_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
   }
};

class duplicate_resolver_plugin_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "duplicate-resolver-route"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
         .factory = [] {
            return std::make_unique<duplicate_resolver_route_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      context.apis().install<peer_context_test_api>(peer_context_test_api::describe(),
                                                    std::make_shared<peer_context_test_api_impl>());
      co_return;
   }
};

class resolver_custom_transport_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "resolver-custom-transport-route"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
         .factory = [] {
            return std::make_unique<resolver_custom_transport_route_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<node_test_api>(node_test_api::describe(), std::make_shared<node_test_api_impl>());
      context.apis().install<peer_context_test_api>(peer_context_test_api::describe(),
                                                    std::make_shared<peer_context_test_api_impl>());
      co_return;
   }
};

class receipt_resolver_application final : public forge::app::application_shell {
 public:
   explicit receipt_resolver_application(std::shared_ptr<receipt_test_state> state) : state_{std::move(state)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "receipt-route-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
         .factory = [] {
            return std::make_unique<receipt_route_publisher_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<receipt_test_api>(receipt_test_api::describe(),
                                               std::make_shared<receipt_test_api_impl>(state_));
      co_return;
   }

 private:
   std::shared_ptr<receipt_test_state> state_;
};

class resolver_protocol_conflict_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "resolver-protocol-conflict"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
         .factory = [] {
            return std::make_unique<resolver_protocol_conflict_plugin>();
         },
      });
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
   }
};

class scripted_resolver_application final : public forge::app::application_shell {
 public:
   explicit scripted_resolver_application(std::shared_ptr<scripted_resolver_state> state) : state_{std::move(state)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "scripted-resolver"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.node"}},
         .factory = [] {
            return std::make_unique<scripted_resolver_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<scripted_resolver_api>(scripted_resolver_api::describe(),
                                                    std::make_shared<scripted_resolver_api_impl>(state_));
      co_return;
   }

 private:
   std::shared_ptr<scripted_resolver_state> state_;
};

class http_server_application final : public forge::app::application_shell {
 public:
   http_server_application(std::shared_ptr<http_publish_state> state, bool middleware = false)
       : state_{std::move(state)}, middleware_{middleware} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(http_server::descriptor());
      if (middleware_) {
         registry.register_plugin(forge::app::plugin_descriptor{
            .id = forge::app::plugin_id{.value = "http-middleware"},
            .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
            .factory = [state = state_] {
               return std::make_unique<http_middleware_plugin>(state);
            },
         });
      }
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "http-cache-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [state = state_] {
            return std::make_unique<http_cache_publisher_plugin>(state);
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<http_cache_api>(http_cache_api::describe(), std::make_shared<http_cache_api_impl>());
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
   bool middleware_ = false;
};

class http_stream_server_application final : public forge::app::application_shell {
 public:
   explicit http_stream_server_application(std::shared_ptr<http_publish_state> state)
       : state_{std::move(state)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(http_server::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "http-middleware"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [state = state_] {
            return std::make_unique<http_middleware_plugin>(state);
         },
      });
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "http-stream-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [state = state_] {
            return std::make_unique<http_stream_publisher_plugin>(state);
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<http_stream_api>(http_stream_api::describe(),
                                              std::make_shared<http_stream_api_impl>(state_));
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class http_empty_server_application final : public forge::app::application_shell {
 public:
   explicit http_empty_server_application(std::shared_ptr<http_publish_state> state)
       : state_{std::move(state)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(http_server::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "http-middleware"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [state = state_] {
            return std::make_unique<http_middleware_plugin>(state);
         },
      });
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "http-empty-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [state = state_] {
            return std::make_unique<http_empty_publisher_plugin>(state);
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<http_empty_api>(http_empty_api::describe(), std::make_shared<http_empty_api_impl>());
      co_return;
   }

 private:
   std::shared_ptr<http_publish_state> state_;
};

class duplicate_http_server_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(http_server::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "duplicate-http-cache-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [] {
            return std::make_unique<duplicate_http_cache_publisher_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<http_cache_api>(http_cache_api::describe(), std::make_shared<http_cache_api_impl>());
      co_return;
   }
};

class late_http_server_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(http_server::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
         .id = forge::app::plugin_id{.value = "late-http-publisher"},
         .dependencies = {forge::app::plugin_id{.value = "forge.plugins.http.server"}},
         .factory = [] {
            return std::make_unique<late_http_publish_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<http_cache_api>(http_cache_api::describe(), std::make_shared<http_cache_api_impl>());
      co_return;
   }
};

[[nodiscard]] const forge::config::field_descriptor& require_field(const forge::config::component_descriptor& descriptor,
                                                                 std::string_view name) {
   const auto found = std::ranges::find_if(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
   BOOST_REQUIRE(found != descriptor.fields.end());
   return *found;
}

[[nodiscard]] bool has_field(const forge::config::component_descriptor& descriptor, std::string_view name) {
   return std::ranges::any_of(descriptor.fields, [&](const auto& field) {
      return field.name == name;
   });
}

[[nodiscard]] std::uint16_t reserve_loopback_port() {
   auto io = boost::asio::io_context{};
   auto acceptor = boost::asio::ip::tcp::acceptor{io};
   acceptor.open(boost::asio::ip::tcp::v4());
   acceptor.bind({boost::asio::ip::make_address("127.0.0.1"), 0});
   const auto port = acceptor.local_endpoint().port();
   acceptor.close();
   return port;
}

[[nodiscard]] forge::config::value key_entry(std::string key_id,
                                           std::string private_key,
                                           std::string input_profile,
                                           std::vector<std::string> purposes) {
   auto purpose_values = forge::config::value::array_type{};
   for (auto& purpose : purposes) {
      purpose_values.emplace_back(std::move(purpose));
   }

   auto object = forge::config::value::object_type{};
   object.emplace("id", forge::config::value{std::move(key_id)});
   object.emplace("private-key", forge::config::value{std::move(private_key)});
   object.emplace("input-profile", forge::config::value{std::move(input_profile)});
   object.emplace("purposes", forge::config::value{std::move(purpose_values)});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value key_entry_without_purposes(std::string key_id,
                                                            std::string private_key,
                                                            std::string input_profile) {
   auto object = forge::config::value::object_type{};
   object.emplace("id", forge::config::value{std::move(key_id)});
   object.emplace("private-key", forge::config::value{std::move(private_key)});
   object.emplace("input-profile", forge::config::value{std::move(input_profile)});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::document signer_config(std::vector<forge::config::value> keys,
                                                  std::string default_output_profile = "forge") {
   auto document = forge::config::document{};
   document.set("plugins.crypto.signer.keys", forge::config::value::array_type(keys.begin(), keys.end()));
   document.set("plugins.crypto.signer.default-output-profile", std::move(default_output_profile));
   return document;
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
   value.publish(forge::p2p::pubsub::topic{.value = "topic"}, std::vector<std::uint8_t>{},
                 forge::plugins::p2p::pubsub::publish_options{});
};

template <typename T>
concept has_pubsub_subscribe = requires(T& value, forge::plugins::p2p::pubsub::handler handler) {
   value.subscribe(forge::p2p::pubsub::topic{.value = "topic"}, std::move(handler),
                   forge::plugins::p2p::pubsub::subscribe_options{});
};

static_assert(!has_metrics<forge::plugins::p2p::node::api>);
static_assert(!has_peers<forge::plugins::p2p::node::api>);
static_assert(!has_pubsub_publish<forge::plugins::p2p::node::api>);
static_assert(!has_pubsub_subscribe<forge::plugins::p2p::node::api>);
static_assert(forge::api::local_interface<crypto_signer::api>);
static_assert(!forge::api::remote_interface<crypto_signer::api>);

} // namespace

BOOST_AUTO_TEST_CASE(http_server_config_is_described_from_schema) {
   auto plugin = http_server::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.http.server");

   const auto& bind_address = require_field(*descriptor, "bind-address");
   BOOST_TEST(bind_address.has_default);
   BOOST_TEST(std::get<std::string>(bind_address.default_value.storage) == "127.0.0.1");

   const auto& port = require_field(*descriptor, "port");
   BOOST_TEST(port.has_default);
   BOOST_TEST(std::get<std::uint64_t>(port.default_value.storage) == 0U);

   const auto& base_path = require_field(*descriptor, "api-base-path");
   BOOST_TEST(base_path.has_default);
   BOOST_TEST(std::get<std::string>(base_path.default_value.storage) == "/");

   const auto& body_limit = require_field(*descriptor, "max-request-body-bytes");
   BOOST_TEST(body_limit.has_default);
   BOOST_TEST(std::get<std::uint64_t>(body_limit.default_value.storage) == 16U * 1024U * 1024U);

   BOOST_TEST(require_field(*descriptor, "max-header-bytes").has_default);
   BOOST_TEST(require_field(*descriptor, "read-timeout-ms").has_default);
   BOOST_TEST(require_field(*descriptor, "idle-timeout-ms").has_default);
}

BOOST_AUTO_TEST_CASE(http_server_rejects_invalid_schema_config) {
   auto plugin = http_server::plugin{};
   auto document = forge::config::document{};
   document.set("plugins.http.server.port", std::uint64_t{70000});

   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.http.server"})),
      http_server::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(http_server_plugin_rejects_invalid_api_base_path_during_configure) {
   auto runtime = forge::asio::runtime{};

   auto empty = http_server::plugin{};
   auto empty_document = forge::config::document{};
   empty_document.set("plugins.http.server.api-base-path", std::string{});
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, empty.configure(forge::config::component_view{empty_document, "plugins.http.server"})),
      http_server::exceptions::invalid_config);

   auto relative = http_server::plugin{};
   auto relative_document = forge::config::document{};
   relative_document.set("plugins.http.server.api-base-path", std::string{"api"});
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        runtime, relative.configure(forge::config::component_view{relative_document, "plugins.http.server"})),
                     http_server::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(http_server_plugin_publishes_typed_api_under_configured_base_path) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   auto app = http_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port) + "/api")};
   auto cache = forge::asio::blocking::run(app.runtime(), forge::http::api::remote<http_cache_api>(client));
   const auto chunk = forge::asio::blocking::run(
      app.runtime(), cache->read(http_read_request{.ref = "alpha", .offset = 7, .limit = 9}));
   BOOST_TEST(chunk.bytes == "alpha:7:9");

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_uses_publish_base_path_override) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/custom";
   auto app = http_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port) + "/custom")};
   auto cache = forge::asio::blocking::run(app.runtime(), forge::http::api::remote<http_cache_api>(client));
   const auto chunk = forge::asio::blocking::run(
      app.runtime(), cache->write(http_write_request{.ref = "beta", .bytes = "payload"}));
   BOOST_TEST(chunk.bytes == "beta:payload");

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_applies_middleware_order_and_short_circuit) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->short_circuit = true;
   auto app = http_server_application{state, true};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   const auto denied = forge::asio::blocking::run(
      app.runtime(), client.async_get("/api/cache/chunks/secure?offset=1&limit=1"));
   BOOST_TEST(static_cast<unsigned>(denied.result()) == static_cast<unsigned>(forge::http::status::unauthorized));
   BOOST_TEST(state->middleware_events == (std::vector<std::string>{"security"}),
              boost::test_tools::per_element());

   auto request = forge::http::request{};
   request.method(forge::http::method::get);
   request.target("/api/cache/chunks/secure?offset=1&limit=1");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");
   const auto allowed = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));
   BOOST_TEST(static_cast<unsigned>(allowed.result()) == static_cast<unsigned>(forge::http::status::ok));
   BOOST_TEST(std::string{allowed[forge::http::field::server]} == "forge-test");
   BOOST_TEST(state->middleware_events ==
                 (std::vector<std::string>{"security", "security", "before"}),
              boost::test_tools::per_element());

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_preserves_stream_framing_through_middleware) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/api";
   auto app = http_stream_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   auto request = forge::http::request{};
   request.method(forge::http::method::get);
   request.target("/api/stream/alpha");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");

   const auto response = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));

   BOOST_TEST(static_cast<unsigned>(response.result()) == static_cast<unsigned>(forge::http::status::ok));
   BOOST_TEST(response.body() == "stream:alpha:payload");
   BOOST_TEST(std::string{response[forge::http::field::server]} == "forge-test");
   const auto has_content_length = response.find(forge::http::field::content_length) != response.end();
   BOOST_TEST(!has_content_length);
   BOOST_TEST(std::string{response[forge::http::field::transfer_encoding]} == "chunked");
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(state->stream_calls.load() == 1U);
   BOOST_TEST(state->stream_chunks.load() == 3U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_stream_middleware_content_type_preserves_stream) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/api";
   state->set_stream_content_type_after_next = true;
   auto app = http_stream_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   auto request = forge::http::request{};
   request.method(forge::http::method::get);
   request.target("/api/stream/alpha");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");

   const auto response = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));

   BOOST_TEST(static_cast<unsigned>(response.result()) == static_cast<unsigned>(forge::http::status::ok));
   BOOST_TEST(response.body() == "stream:alpha:payload");
   BOOST_TEST(std::string{response[forge::http::field::content_type]} == "application/x-ndjson");
   BOOST_TEST(std::string{response[forge::http::field::server]} == "forge-test");
   const auto has_content_length = response.find(forge::http::field::content_length) != response.end();
   BOOST_TEST(!has_content_length);
   BOOST_TEST(std::string{response[forge::http::field::transfer_encoding]} == "chunked");
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(state->stream_calls.load() == 1U);
   BOOST_TEST(state->stream_chunks.load() == 3U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_preserves_absent_content_type_through_middleware) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/api";
   auto app = http_empty_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   auto request = forge::http::request{};
   request.method(forge::http::method::delete_);
   request.target("/api/empty/alpha");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");

   const auto response = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));

   BOOST_TEST(static_cast<unsigned>(response.result()) == static_cast<unsigned>(forge::http::status::no_content));
   BOOST_TEST(response.body().empty());
   const auto has_content_type = response.find(forge::http::field::content_type) != response.end();
   BOOST_TEST(!has_content_type);
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(std::string{response[forge::http::field::server]} == "forge-test");

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_stream_middleware_replacement_does_not_leak_original_body) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/api";
   state->replace_stream_after_next = true;
   auto app = http_stream_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   auto request = forge::http::request{};
   request.method(forge::http::method::get);
   request.target("/api/stream/alpha");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");

   const auto response = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));

   BOOST_TEST(static_cast<unsigned>(response.result()) == static_cast<unsigned>(forge::http::status::forbidden));
   BOOST_TEST(response.body() == "blocked");
   const auto has_transfer_encoding = response.find(forge::http::field::transfer_encoding) != response.end();
   BOOST_TEST(!has_transfer_encoding);
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(state->stream_calls.load() == 1U);
   BOOST_TEST(state->stream_chunks.load() == 0U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_empty_stream_middleware_replacement_clears_hidden_token) {
   const auto port = reserve_loopback_port();
   auto state = std::make_shared<http_publish_state>();
   state->base_path = "/api";
   state->empty_replace_stream_after_next = true;
   auto app = http_stream_server_application{state};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});
   config.set("plugins.http.server.api-base-path", std::string{"/api"});

   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto client = forge::http::client{app.runtime(), forge::http::parse_base_url("http://127.0.0.1:" +
                                                                            std::to_string(port))};
   auto request = forge::http::request{};
   request.method(forge::http::method::get);
   request.target("/api/stream/alpha");
   request.version(11);
   request.set(forge::http::field::authorization, "Bearer test");

   const auto response = forge::asio::blocking::run(app.runtime(), client.async_request(std::move(request)));

   BOOST_TEST(static_cast<unsigned>(response.result()) == static_cast<unsigned>(forge::http::status::forbidden));
   BOOST_TEST(response.body().empty());
   const auto has_transfer_encoding = response.find(forge::http::field::transfer_encoding) != response.end();
   BOOST_TEST(!has_transfer_encoding);
   BOOST_TEST(!has_internal_forge_header(response));
   BOOST_TEST(state->stream_calls.load() == 1U);
   BOOST_TEST(state->stream_chunks.load() == 0U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(http_server_plugin_rejects_duplicate_publication_on_startup) {
   const auto port = reserve_loopback_port();
   auto app = duplicate_http_server_application{};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});

   app.configure(config);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.startup()), forge::http::exceptions::conflict);
}

BOOST_AUTO_TEST_CASE(http_server_plugin_rejects_late_publication_after_startup_closed) {
   const auto port = reserve_loopback_port();
   auto app = late_http_server_application{};
   auto config = forge::config::document{};
   config.set("plugins.http.server.port", std::uint64_t{port});

   app.configure(config);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.startup()),
                     http_server::exceptions::publication_closed);
}

BOOST_AUTO_TEST_CASE(crypto_signer_config_is_redacted_and_local_only) {
   auto plugin = crypto_signer::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.crypto.signer");

   const auto& keys = require_field(*descriptor, "keys");
   BOOST_TEST(keys.secret);
   BOOST_TEST(static_cast<int>(keys.kind) == static_cast<int>(forge::schema::value_kind::object_list));
   BOOST_TEST(has_field(*descriptor, "default-output-profile"));

   auto registry = forge::config::component_registry{};
   registry.add(*descriptor);

   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   auto document = signer_config(
      {key_entry("provider",
                 forge::crypto::asymmetric::encoding::forge().format(key),
                 "forge",
                 {"storage.receipt"})});
   const auto redacted = forge::config::redact(document, registry);
   const auto* value = redacted.try_get("plugins.crypto.signer.keys");
   BOOST_REQUIRE(value != nullptr);
   const auto* text = std::get_if<std::string>(&value->storage);
   BOOST_REQUIRE(text != nullptr);
   BOOST_TEST(*text == "<redacted>");
}

BOOST_AUTO_TEST_CASE(crypto_signer_config_decodes_through_public_schema) {
   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   const auto document = signer_config(
      {key_entry("provider",
                 forge::crypto::asymmetric::encoding::forge().format(key),
                 "forge",
                 {"storage.receipt"})});

   const auto decoded = forge::config::decode<crypto_signer::config>(document, "plugins.crypto.signer");
   BOOST_TEST(decoded.ok());
   BOOST_REQUIRE_EQUAL(decoded.value.keys.size(), 1U);
   BOOST_TEST(decoded.value.keys.front().id == "provider");
   BOOST_TEST(decoded.value.keys.front().input_profile == "forge");
   BOOST_REQUIRE_EQUAL(decoded.value.keys.front().purposes.size(), 1U);
   BOOST_TEST(decoded.value.keys.front().purposes.front() == "storage.receipt");
   BOOST_TEST(decoded.value.default_output_profile == "forge");
}

BOOST_AUTO_TEST_CASE(crypto_signer_structured_keys_are_not_cli_or_env_fields) {
   auto plugin = crypto_signer::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());

   auto registry = forge::config::component_registry{};
   registry.add(*descriptor);

   const auto help = forge::program_options::help(registry, "FORGE options");
   BOOST_TEST(help.find("plugins.crypto.signer.keys") == std::string::npos);

   const char* argv[] = {"tool", "--plugins.crypto.signer.keys=provider"};
   const auto parsed = forge::program_options::parse(2, argv, registry);
   BOOST_TEST(!parsed.ok());
   BOOST_TEST(parsed.document.try_get("plugins.crypto.signer.keys") == nullptr);

   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   const auto document = signer_config(
      {key_entry("provider",
                 forge::crypto::asymmetric::encoding::forge().format(key),
                 "forge",
                 {"storage.receipt"})});

   const auto written = forge::env::write_document(document, registry, {.prefix = "FORGE"});
   BOOST_TEST(written.ok());
   BOOST_TEST(written.text.find("FORGE_PLUGINS_CRYPTO_SIGNER_KEYS") == std::string::npos);

   const auto example = forge::env::write_example(registry, {.prefix = "FORGE"});
   BOOST_TEST(example.ok());
   BOOST_TEST(example.text.find("FORGE_PLUGINS_CRYPTO_SIGNER_KEYS") == std::string::npos);

   const auto read = forge::env::read_document(
      "FORGE_PLUGINS_CRYPTO_SIGNER_KEYS=provider\n",
      registry,
      {.prefix = "FORGE", .unknown_variables = forge::env::unknown_variable_policy::error});
   BOOST_TEST(!read.ok());
   BOOST_REQUIRE_EQUAL(read.diagnostics.size(), 1U);
   BOOST_TEST(read.diagnostics.front().code == "env.unknown");
   BOOST_TEST(read.value.try_get("plugins.crypto.signer.keys") == nullptr);
}

BOOST_AUTO_TEST_CASE(crypto_signer_rejects_malformed_private_key_without_leaking_secret) {
   auto plugin = crypto_signer::plugin{};
   const auto bad_key = std::string{"PVT_SECP256K1_not-a-valid-secret!!!!"};
   auto document = signer_config({key_entry("provider", bad_key, "forge", {"storage.receipt"})});

   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_EXCEPTION(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"})),
      crypto_signer::exceptions::invalid_key,
      [&](const auto& error) {
         const auto text = std::string{error.what()};
         return text.find("provider") != std::string::npos &&
                text.find(bad_key) == std::string::npos &&
                text.find("not-a-valid-secret") == std::string::npos &&
                text.find("base58_str") == std::string::npos;
      });
}

BOOST_AUTO_TEST_CASE(crypto_signer_rejects_empty_private_key_through_schema) {
   auto plugin = crypto_signer::plugin{};
   auto document = signer_config({key_entry("provider", "", "forge", {"storage.receipt"})});

   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_EXCEPTION(
      forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"})),
      crypto_signer::exceptions::invalid_config,
      [](const auto& error) {
         const auto text = std::string{error.what()};
         return text.find("plugins.crypto.signer.keys[0].private-key") != std::string::npos &&
                text.find("schema.non_empty") != std::string::npos;
      });
}

BOOST_AUTO_TEST_CASE(crypto_signer_requires_explicit_non_empty_purposes) {
   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   const auto private_key = forge::crypto::asymmetric::encoding::forge().format(key);

   auto runtime = forge::asio::runtime{};

   auto missing = crypto_signer::plugin{};
   auto missing_document = signer_config({key_entry_without_purposes("missing", private_key, "forge")});
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, missing.configure(forge::config::component_view{missing_document, "plugins.crypto.signer"})),
      crypto_signer::exceptions::invalid_config);

   auto empty = crypto_signer::plugin{};
   auto empty_document = signer_config({key_entry("empty", private_key, "forge", {})});
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, empty.configure(forge::config::component_view{empty_document, "plugins.crypto.signer"})),
      crypto_signer::exceptions::invalid_config);

   auto blank = crypto_signer::plugin{};
   auto blank_document = signer_config({key_entry("blank", private_key, "forge", {""})});
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(runtime, blank.configure(forge::config::component_view{blank_document, "plugins.crypto.signer"})),
      crypto_signer::exceptions::invalid_config);
}

BOOST_AUTO_TEST_CASE(crypto_signer_signs_k1_digest_with_antelope_output) {
   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   auto plugin = crypto_signer::plugin{};
   auto document = signer_config(
      {key_entry("provider",
                 forge::crypto::asymmetric::encoding::forge().format(key),
                 "forge",
                 {"storage.receipt", "storage.audit"})});

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"}));

   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto api = apis.get<crypto_signer::api>(crypto_signer::api::ref());
   const auto digest = forge::crypto::sha256::hash("receipt-payload");
   const auto response = forge::asio::blocking::run(
      runtime,
      api->sign(crypto_signer::request{
         .key_id = "provider",
         .purpose = "storage.receipt",
         .digest = digest,
         .required_algorithm = crypto_signer::key_algorithm::secp256k1,
         .output_profile = "antelope",
      }));

   const auto signature_text = response.signature_text();
   BOOST_TEST(response.key_id == "provider");
   BOOST_TEST(response.algorithm == crypto_signer::key_algorithm::secp256k1);
   BOOST_TEST(response.output_profile == "antelope");
   BOOST_TEST(response.public_key.starts_with("EOS"));
   BOOST_TEST(signature_text.starts_with("SIG_K1_"));

   const auto signature = forge::crypto::asymmetric::encoding::antelope().parse_signature(signature_text);
   const auto recovered = forge::crypto::asymmetric::public_key{signature, digest, true};
   BOOST_TEST(recovered.to_string({}) == key.get_public_key().to_string({}));
}

BOOST_AUTO_TEST_CASE(crypto_signer_sugar_uses_configured_default_output_profile) {
   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   auto plugin = crypto_signer::plugin{};
   auto document = signer_config(
      {key_entry("provider",
                 forge::crypto::asymmetric::encoding::forge().format(key),
                 "forge",
                 {"storage.receipt"})},
      "antelope");

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"}));

   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto api = apis.get<crypto_signer::api>(crypto_signer::api::ref());
   const auto digest = forge::crypto::sha256::hash("receipt-payload");
   const auto defaulted = forge::asio::blocking::run(
      runtime,
      api->sign("provider",
                digest,
                crypto_signer::options{
                   .purpose = "storage.receipt",
                   .required_algorithm = crypto_signer::key_algorithm::secp256k1,
                }));

   BOOST_TEST(defaulted.output_profile == "antelope");
   BOOST_TEST(defaulted.signature_text().starts_with("SIG_K1_"));

   const auto overridden = forge::asio::blocking::run(
      runtime,
      api->sign("provider",
                digest,
                crypto_signer::options{
                   .purpose = "storage.receipt",
                   .required_algorithm = crypto_signer::key_algorithm::secp256k1,
                   .output_profile = "forge",
                }));

   BOOST_TEST(overridden.output_profile == "forge");
   BOOST_TEST(overridden.signature_text().starts_with("SIG_SECP256K1_"));
}

BOOST_AUTO_TEST_CASE(crypto_signer_supports_p256_and_ed25519_without_k1_assumptions) {
   const auto p256_key = forge::crypto::asymmetric::private_key::generate<forge::crypto::p256::private_key_shim>();
   const auto ed25519_key = forge::crypto::asymmetric::private_key::generate<forge::crypto::ed25519::private_key_shim>();
   auto plugin = crypto_signer::plugin{};
   auto document = signer_config(
      {key_entry("p256", forge::crypto::asymmetric::encoding::forge().format(p256_key), "forge", {"api.auth"}),
       key_entry("ed25519", forge::crypto::asymmetric::encoding::forge().format(ed25519_key), "forge", {"api.auth"})});

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"}));

   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto api = apis.get<crypto_signer::api>(crypto_signer::api::ref());

   const auto digest = forge::crypto::sha256::hash("auth-payload");
   const auto p256 = forge::asio::blocking::run(
      runtime,
      api->sign(crypto_signer::request{
         .key_id = "p256",
         .purpose = "api.auth",
         .digest = digest,
         .required_algorithm = crypto_signer::key_algorithm::p256,
      }));
   const auto p256_signature = forge::crypto::asymmetric::encoding::forge().parse_signature(
      std::string{p256.signature.begin(), p256.signature.end()});
   const auto p256_recovered = forge::crypto::asymmetric::public_key{p256_signature, digest, true};
   BOOST_TEST(p256_recovered.to_string({}) == p256_key.get_public_key().to_string({}));

   const auto ed25519 = forge::asio::blocking::run(
      runtime,
      api->sign(crypto_signer::request{
         .key_id = "ed25519",
         .purpose = "api.auth",
         .digest = digest,
         .required_algorithm = crypto_signer::key_algorithm::ed25519,
      }));
   const auto ed25519_signature = forge::crypto::asymmetric::encoding::forge().parse_signature(
      std::string{ed25519.signature.begin(), ed25519.signature.end()});
   const auto ed25519_public = forge::crypto::asymmetric::encoding::forge().parse_public(ed25519.public_key);
   BOOST_TEST(ed25519_public.verify(digest.to_uint8_span(), ed25519_signature));
}

BOOST_AUTO_TEST_CASE(crypto_signer_enforces_allowed_purpose_and_algorithm) {
   const auto key = forge::crypto::asymmetric::private_key::generate<forge::crypto::secp256k1::private_key_shim>();
   auto plugin = crypto_signer::plugin{};
   auto document = signer_config(
      {key_entry("provider", forge::crypto::asymmetric::encoding::forge().format(key), "forge", {"storage.receipt"})});

   auto runtime = forge::asio::runtime{};
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, "plugins.crypto.signer"}));

   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));
   auto api = apis.get<crypto_signer::api>(crypto_signer::api::ref());
   const auto digest = forge::crypto::sha256::hash("receipt-payload");

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->sign(crypto_signer::request{
            .key_id = "missing",
            .purpose = "storage.receipt",
            .digest = digest,
         })),
      crypto_signer::exceptions::key_not_found);

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->sign(crypto_signer::request{
            .key_id = "provider",
            .purpose = "storage.audit",
            .digest = digest,
         })),
      crypto_signer::exceptions::purpose_denied);

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->sign(crypto_signer::request{
            .key_id = "provider",
            .purpose = "storage.receipt",
            .digest = digest,
            .required_algorithm = crypto_signer::key_algorithm::ed25519,
         })),
      crypto_signer::exceptions::unsupported_algorithm);

   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         api->sign(crypto_signer::request{
            .key_id = "provider",
            .purpose = "storage.receipt",
            .digest = digest,
            .output_profile = "bitcoin",
         })),
      crypto_signer::exceptions::unsupported_profile);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_config_is_described_from_public_schema) {
   auto plugin = forge::plugins::p2p::node::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.p2p.node");

   const auto& listen = require_field(*descriptor, "listen");
   BOOST_TEST(static_cast<int>(listen.kind) == static_cast<int>(forge::schema::value_kind::string_list));
   BOOST_TEST(listen.has_default);

   const auto& bootstrap = require_field(*descriptor, "bootstrap");
   BOOST_TEST(static_cast<int>(bootstrap.kind) == static_cast<int>(forge::schema::value_kind::string_list));
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

   app.configure(test_p2p_config(test_peer(18)));
   forge::asio::blocking::run(app.runtime(), app.startup());

   BOOST_TEST(app.apis().describe({.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0}) != nullptr);
   BOOST_TEST(log.entries == (std::vector<std::string>{"routes.published", "routes.startup"}),
              boost::test_tools::per_element());

   forge::asio::blocking::run(app.runtime(), app.shutdown());
   BOOST_TEST(log.entries == (std::vector<std::string>{"routes.published", "routes.startup", "routes.shutdown"}),
              boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_duplicate_protocol_contributions_before_startup) {
   auto app = duplicate_p2p_plugin_application{};

   app.configure(test_p2p_config(test_peer(19)));
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.initialize()),
                     forge::plugins::p2p::node::exceptions::route_conflict);
}

BOOST_AUTO_TEST_CASE(p2p_node_api_rejects_facade_calls_before_initialize) {
   auto runtime = forge::asio::runtime{};
   auto plugin = forge::plugins::p2p::node::plugin{};
   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto p2p = apis.get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)p2p->local_peer(), forge::plugins::p2p::node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->local_endpoint(), forge::plugins::p2p::node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->local_endpoints(), forge::plugins::p2p::node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)p2p->network_info(), forge::plugins::p2p::node::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              p2p->remote<node_test_api>(
                                                 test_peer(10), forge::p2p::protocol_id{.value = "/forge/api/node-test/1"})),
                     forge::plugins::p2p::node::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_listens_from_config_and_exposes_local_endpoints) {
   const auto local_peer = test_peer(20);
   auto config = test_p2p_config(local_peer);
   config.set("plugins.p2p.node.listen",
              forge::config::value::array_type{forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"},
                                             forge::config::value{"/ip4/127.0.0.1/tcp/0"}});

   auto app = p2p_only_application{};
   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   const auto p2p = app.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
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

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_without_peer_id_does_not_inject_test_peer) {
   auto config = test_p2p_config();
   config.set("plugins.p2p.node.listen", forge::config::value::array_type{forge::config::value{"/ip4/127.0.0.1/tcp/0"}});

   auto app = p2p_only_application{};
   app.configure(config);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.startup()),
                     forge::p2p::exceptions::invalid_identity);
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_opens_remote_api_over_p2p_stream) {
   const auto server_peer = test_peer(30);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto log = plugin_log{};
   auto server = p2p_plugin_application{log};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   const auto client_peer = test_peer(31);
   auto client_config = test_p2p_config(client_peer);
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});

   auto client = p2p_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto client_p2p = client.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   auto remote = forge::asio::blocking::run(
      client.runtime(),
      client_p2p->remote<node_test_api>(server_p2p->local_peer(),
                                        forge::p2p::protocol_id{.value = "/forge/api/node-test/1"}));
   const auto response = forge::asio::blocking::run(client.runtime(), remote->ping(41));
   BOOST_TEST(response == 42);

   auto context_remote = forge::asio::blocking::run(
      client.runtime(),
      client_p2p->remote<peer_context_test_api>(server_p2p->local_peer(),
                                                forge::p2p::protocol_id{.value = "/forge/api/node-test/1"}));
   const auto observed_peer = forge::asio::blocking::run(client.runtime(), context_remote->remote_peer("probe"));
   BOOST_REQUIRE(observed_peer.ends_with(":probe"));
   const auto observed_peer_id = forge::p2p::peer_id::from_string(observed_peer.substr(0, observed_peer.size() - 6));
   BOOST_TEST(forge::p2p::valid_peer_id(observed_peer_id));

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_node_plugin_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.max-inflight-per-peer", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.listen", forge::config::value::array_type{forge::config::value{"127.0.0.1:0"}});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.api.max-frame-size", std::uint64_t{0});
      auto app = p2p_only_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::node::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.path.policy", std::string{"teleport"});
      auto app = p2p_only_application{};
      BOOST_CHECK_EXCEPTION(
         app.configure(config),
         forge::plugins::p2p::node::exceptions::invalid_config,
         [](const auto& error) {
            const auto text = std::string{error.what()};
            return text.find("plugins.p2p.node.path.policy") != std::string::npos &&
                   text.find("config.type") != std::string::npos;
         });
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.path.policy", std::string{"relay-only"});
      auto app = p2p_only_application{};
      BOOST_CHECK_NO_THROW(app.configure(config));
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.relay.trust", std::string{"public-allowed"});
      auto app = p2p_only_application{};
      BOOST_CHECK_NO_THROW(app.configure(config));
   }

   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.node.relay.trust", std::string{"everyone"});
      auto app = p2p_only_application{};
      BOOST_CHECK_EXCEPTION(
         app.configure(config),
         forge::plugins::p2p::node::exceptions::invalid_config,
         [](const auto& error) {
            const auto text = std::string{error.what()};
            return text.find("plugins.p2p.node.relay.trust") != std::string::npos &&
                   text.find("config.type") != std::string::npos;
         });
   }
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_plugin_config_is_described_from_public_schema) {
   auto plugin = forge::plugins::p2p::diagnostics::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.p2p.diagnostics");

   const auto& max_peers = require_field(*descriptor, "max-peers");
   BOOST_TEST(max_peers.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_peers.default_value.storage) > 0U);

   const auto& max_sessions = require_field(*descriptor, "max-sessions");
   BOOST_TEST(max_sessions.has_default);
   BOOST_TEST(std::get<std::uint64_t>(max_sessions.default_value.storage) > 0U);
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_api_rejects_facade_calls_before_initialize) {
   auto runtime = forge::asio::runtime{};
   auto plugin = forge::plugins::p2p::diagnostics::plugin{};
   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto diagnostics = apis.get<forge::plugins::p2p::diagnostics::api>(
      {.id = {"forge.plugins.p2p.diagnostics"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)diagnostics->snapshot(), forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->network(), forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->resources(), forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->pubsub(), forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->peers(), forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)diagnostics->peer(test_peer(90)),
                     forge::plugins::p2p::diagnostics::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_diagnostics_plugin_reports_live_p2p_node_state) {
   const auto server_peer = test_peer(91);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto log = plugin_log{};
   auto server = p2p_plugin_application{log};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   const auto client_peer = test_peer(92);
   auto client_config = test_p2p_config(client_peer);
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});

   auto client = diagnostics_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto client_p2p = client.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   auto diagnostics = client.apis().get<forge::plugins::p2p::diagnostics::api>(
      {.id = {"forge.plugins.p2p.diagnostics"}, .major = 1, .min_revision = 0});

   auto remote = forge::asio::blocking::run(
      client.runtime(),
      client_p2p->remote<node_test_api>(server_p2p->local_peer(),
                                        forge::p2p::protocol_id{.value = "/forge/api/node-test/1"}));
   const auto response = forge::asio::blocking::run(client.runtime(), remote->ping(10));
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
   BOOST_CHECK_THROW((void)diagnostics->peer(test_peer(93)), forge::plugins::p2p::diagnostics::exceptions::not_found);

   const auto descriptors = client.views().descriptors();
   BOOST_CHECK(std::ranges::any_of(descriptors, [](const auto& descriptor) {
      return descriptor.id == "forge.plugins.p2p.node.peers";
   }));
   auto peers_view = forge::asio::blocking::run(
      client.runtime(),
      client.views().snapshot("forge.plugins.p2p.node.peers", forge::app::view_query{.limit = 1}));
   BOOST_CHECK(peers_view.descriptor.kind == forge::app::view_kind::table);
   BOOST_REQUIRE_EQUAL(peers_view.page.rows.size(), 1U);
   BOOST_REQUIRE(!peers_view.page.rows.front().cells.empty());
   BOOST_TEST(peers_view.page.rows.front().cells.front() == server_peer.to_string());

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_config_is_described_from_public_schema) {
   auto plugin = forge::plugins::p2p::pubsub::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.p2p.pubsub");

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
   auto runtime = forge::asio::runtime{};
   auto plugin = forge::plugins::p2p::pubsub::plugin{};
   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto pubsub = apis.get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});

   BOOST_CHECK_THROW((void)pubsub->snapshot(), forge::plugins::p2p::pubsub::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)pubsub->subscriptions(), forge::plugins::p2p::pubsub::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        runtime, pubsub->publish(forge::p2p::pubsub::topic{.value = "forge.before-init"}, {1, 2, 3})),
                     forge::plugins::p2p::pubsub::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.pubsub.max-topics", std::uint64_t{0});
      auto app = pubsub_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::pubsub::exceptions::invalid_config);
   }
   {
      auto config = test_p2p_config();
      config.set("plugins.p2p.pubsub.handler-deadline-ms", std::uint64_t{0});
      auto app = pubsub_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::pubsub::exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_requests_core_pubsub_capability_before_startup) {
   auto config = test_p2p_config(test_peer(94));
   config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   config.set("plugins.p2p.pubsub.sign-publishes", false);

   auto app = pubsub_application{};
   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   auto subscription = forge::asio::blocking::run(
      app.runtime(),
      pubsub->subscribe(
         forge::p2p::pubsub::topic{.value = "forge.local"},
         [](forge::plugins::p2p::pubsub::message) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            co_return forge::p2p::pubsub::validation_result::accept;
         }));

   BOOST_TEST(subscription.id != 0U);
   BOOST_TEST(pubsub->snapshot().core.topics == 1U);
   BOOST_TEST(pubsub->subscriptions().size() == 1U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_serializes_first_join_per_topic) {
   auto source_state = std::make_shared<fake_pubsub_source_state>();
   auto app = fake_pubsub_application{source_state};
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   const auto topic = forge::p2p::pubsub::topic{.value = "forge.fake.pending"};
   auto first = std::make_shared<subscribe_task_result>();
   auto second = std::make_shared<subscribe_task_result>();
   auto handler = [](forge::plugins::p2p::pubsub::message) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
      co_return forge::p2p::pubsub::validation_result::accept;
   };

   forge::asio::blocking::run(
      app.runtime(), [&]() -> boost::asio::awaitable<void> {
         auto executor = co_await boost::asio::this_coro::executor;
         boost::asio::co_spawn(
            executor,
            [pubsub, topic, first, handler]() mutable -> boost::asio::awaitable<void> {
               try {
                  first->complete(co_await pubsub->subscribe(topic, handler));
               } catch (...) {
                  first->fail(std::current_exception());
               }
            },
            boost::asio::detached);
         boost::asio::co_spawn(
            executor,
            [pubsub, topic, second, handler]() mutable -> boost::asio::awaitable<void> {
               try {
                  second->complete(co_await pubsub->subscribe(topic, handler));
               } catch (...) {
                  second->fail(std::current_exception());
               }
            },
            boost::asio::detached);

         BOOST_REQUIRE(co_await async_wait_for_condition([&] { return source_state->joins() == 1U; },
                                                        std::chrono::seconds{1}));
         auto settle_timer = boost::asio::steady_timer{executor, std::chrono::milliseconds{50}};
         co_await settle_timer.async_wait(boost::asio::use_awaitable);
         BOOST_TEST(!first->finished());
         BOOST_TEST(!second->finished());
         source_state->release(false);
         BOOST_REQUIRE(co_await async_wait_for_condition([&] { return first->finished() && second->finished(); },
                                                        std::chrono::seconds{1}));
      }());

   BOOST_TEST(!first->failed());
   BOOST_TEST(!second->failed());
   BOOST_TEST(source_state->joins() == 1U);
   BOOST_TEST(pubsub->snapshot().topics == 1U);
   BOOST_TEST(pubsub->subscriptions().size() == 2U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_failed_first_join_clears_pending_topic) {
   auto source_state = std::make_shared<fake_pubsub_source_state>();
   auto app = fake_pubsub_application{source_state};
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   const auto topic = forge::p2p::pubsub::topic{.value = "forge.fake.failed"};
   auto first = std::make_shared<subscribe_task_result>();
   auto second = std::make_shared<subscribe_task_result>();
   auto handler = [](forge::plugins::p2p::pubsub::message) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
      co_return forge::p2p::pubsub::validation_result::accept;
   };

   forge::asio::blocking::run(
      app.runtime(), [&]() -> boost::asio::awaitable<void> {
         auto executor = co_await boost::asio::this_coro::executor;
         boost::asio::co_spawn(
            executor,
            [pubsub, topic, first, handler]() mutable -> boost::asio::awaitable<void> {
               try {
                  first->complete(co_await pubsub->subscribe(topic, handler));
               } catch (...) {
                  first->fail(std::current_exception());
               }
            },
            boost::asio::detached);
         boost::asio::co_spawn(
            executor,
            [pubsub, topic, second, handler]() mutable -> boost::asio::awaitable<void> {
               try {
                  second->complete(co_await pubsub->subscribe(topic, handler));
               } catch (...) {
                  second->fail(std::current_exception());
               }
            },
            boost::asio::detached);

         BOOST_REQUIRE(co_await async_wait_for_condition([&] { return source_state->joins() == 1U; },
                                                        std::chrono::seconds{1}));
         source_state->release(true);
         BOOST_REQUIRE(co_await async_wait_for_condition([&] { return first->finished() && second->finished(); },
                                                        std::chrono::seconds{1}));
      }());

   BOOST_TEST(first->failed());
   BOOST_TEST(second->failed());
   BOOST_TEST(pubsub->snapshot().topics == 0U);
   BOOST_TEST(pubsub->subscriptions().empty());

   source_state->release(false);
   const auto retry = forge::asio::blocking::run(app.runtime(), pubsub->subscribe(topic, handler));
   BOOST_TEST(retry.subject.value == topic.value);
   BOOST_TEST(source_state->joins() == 2U);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_publishes_and_subscribes_raw_and_typed_messages) {
   const auto subscriber_peer = test_peer(95);
   auto subscriber_config = test_p2p_config(subscriber_peer);
   subscriber_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                           forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   subscriber_config.set("plugins.p2p.pubsub.sign-publishes", false);

   auto subscriber = pubsub_application{};
   subscriber.configure(subscriber_config);
   forge::asio::blocking::run(subscriber.runtime(), subscriber.startup());

   auto subscriber_p2p = subscriber.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto subscriber_endpoint = subscriber_p2p->local_endpoint();
   BOOST_REQUIRE(subscriber_endpoint.has_value());

   const auto publisher_peer = test_peer(96);
   auto publisher_config = test_p2p_config(publisher_peer);
   publisher_config.set("plugins.p2p.node.bootstrap",
                        forge::config::value::array_type{forge::config::value{subscriber_endpoint->to_string()}});
   publisher_config.set("plugins.p2p.pubsub.sign-publishes", false);

   auto publisher = pubsub_application{};
   publisher.configure(publisher_config);
   forge::asio::blocking::run(publisher.runtime(), publisher.startup());

   auto received = std::make_shared<received_pubsub_messages>();
   auto subscriber_pubsub = subscriber.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   auto publisher_pubsub = publisher.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});

   const auto raw_topic = forge::p2p::pubsub::topic{.value = "forge.plugins.raw"};
   const auto typed_topic = forge::p2p::pubsub::topic{.value = "forge.plugins.typed"};
   auto first = forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         raw_topic, [received](forge::plugins::p2p::pubsub::message message) mutable
                       -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message), forge::p2p::pubsub::validation_result::accept);
            co_return forge::p2p::pubsub::validation_result::accept;
         }));
   auto second = forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         raw_topic, [received](forge::plugins::p2p::pubsub::message message) mutable
                       -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message), forge::p2p::pubsub::validation_result::accept);
            co_return forge::p2p::pubsub::validation_result::accept;
         }));
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe<pubsub_payload>(
         typed_topic, [received](forge::plugins::p2p::pubsub::typed_message<pubsub_payload> message) mutable
                         -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message));
            co_return forge::p2p::pubsub::validation_result::accept;
         }));

   BOOST_TEST(first.subject.value == raw_topic.value);
   BOOST_TEST(second.id != first.id);
   BOOST_REQUIRE_MESSAGE(wait_for_pubsub_peer(*publisher_pubsub.shared(), std::chrono::seconds{5}),
                         "publisher did not learn a remote PubSub topic subscription");

   (void)forge::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(raw_topic, std::vector<std::uint8_t>{1, 2, 3, 4}));
   (void)forge::asio::blocking::run(
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
      BOOST_TEST(forge::p2p::valid_peer_id(received->raw.front().source));
      BOOST_TEST(received->raw.front().data == (std::vector<std::uint8_t>{1, 2, 3, 4}),
                 boost::test_tools::per_element());
      BOOST_TEST(received->typed.front().source.to_string() == received->raw.front().source.to_string());
      BOOST_TEST(received->typed.front().value.text == "hello");
      BOOST_TEST(received->typed.front().value.value == 7U);
   }

   forge::asio::blocking::run(subscriber.runtime(), subscriber_pubsub->unsubscribe(first));
   BOOST_TEST(subscriber_pubsub->subscriptions().size() == 2U);
   forge::asio::blocking::run(subscriber.runtime(), subscriber_pubsub->unsubscribe(second));
   BOOST_TEST(subscriber_pubsub->subscriptions().size() == 1U);

   forge::asio::blocking::run(publisher.runtime(), publisher.shutdown());
   forge::asio::blocking::run(subscriber.runtime(), subscriber.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_aggregates_handler_results_and_deadlines) {
   const auto subscriber_peer = test_peer(98);
   auto subscriber_config = test_p2p_config(subscriber_peer);
   subscriber_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                           forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});
   subscriber_config.set("plugins.p2p.pubsub.sign-publishes", false);

   auto subscriber = pubsub_application{};
   subscriber.configure(subscriber_config);
   forge::asio::blocking::run(subscriber.runtime(), subscriber.startup());

   auto subscriber_p2p = subscriber.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto subscriber_endpoint = subscriber_p2p->local_endpoint();
   BOOST_REQUIRE(subscriber_endpoint.has_value());

   auto publisher_config = test_p2p_config(test_peer(99));
   publisher_config.set("plugins.p2p.node.bootstrap",
                        forge::config::value::array_type{forge::config::value{subscriber_endpoint->to_string()}});
   publisher_config.set("plugins.p2p.pubsub.sign-publishes", false);

   auto publisher = pubsub_application{};
   publisher.configure(publisher_config);
   forge::asio::blocking::run(publisher.runtime(), publisher.startup());

   auto received = std::make_shared<received_pubsub_messages>();
   auto subscriber_pubsub = subscriber.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   auto publisher_pubsub = publisher.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});

   const auto aggregate_topic = forge::p2p::pubsub::topic{.value = "forge.plugins.aggregate"};
   const auto timeout_topic = forge::p2p::pubsub::topic{.value = "forge.plugins.timeout"};
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](forge::plugins::p2p::pubsub::message message) mutable
                             -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message), forge::p2p::pubsub::validation_result::ignore);
            co_return forge::p2p::pubsub::validation_result::ignore;
         }));
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [](forge::plugins::p2p::pubsub::message)
                             -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            throw forge::plugins::p2p::pubsub::exceptions::handler_limit{"test handler failure"};
         }));
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](forge::plugins::p2p::pubsub::message message) mutable
                             -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message), forge::p2p::pubsub::validation_result::accept);
            co_return forge::p2p::pubsub::validation_result::accept;
         }));
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         aggregate_topic, [received](forge::plugins::p2p::pubsub::message message) mutable
                             -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            received->push(std::move(message), forge::p2p::pubsub::validation_result::reject);
            co_return forge::p2p::pubsub::validation_result::reject;
         }));
   (void)forge::asio::blocking::run(
      subscriber.runtime(),
      subscriber_pubsub->subscribe(
         timeout_topic,
         [](forge::plugins::p2p::pubsub::message) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
            auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
            timer.expires_after(std::chrono::milliseconds{100});
            co_await timer.async_wait(boost::asio::use_awaitable);
            co_return forge::p2p::pubsub::validation_result::accept;
         },
         forge::plugins::p2p::pubsub::subscribe_options{.handler_deadline = std::chrono::milliseconds{10}}));

   BOOST_REQUIRE_MESSAGE(wait_for_pubsub_peer(*publisher_pubsub.shared(), std::chrono::seconds{5}),
                         "publisher did not learn a remote PubSub topic subscription");

   (void)forge::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(aggregate_topic, std::vector<std::uint8_t>{9}));
   BOOST_REQUIRE_MESSAGE(
      wait_for_pubsub_snapshot(
         *subscriber_pubsub.shared(),
         [](const forge::plugins::p2p::pubsub::snapshot& snapshot) {
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

   (void)forge::asio::blocking::run(
      publisher.runtime(), publisher_pubsub->publish(timeout_topic, std::vector<std::uint8_t>{10}));
   BOOST_REQUIRE_MESSAGE(
      wait_for_pubsub_snapshot(
         *subscriber_pubsub.shared(),
         [](const forge::plugins::p2p::pubsub::snapshot& snapshot) {
            return snapshot.messages_ignored >= 1 && snapshot.handler_failures >= 2;
         },
         std::chrono::seconds{5}),
      "PubSub handler timeout did not finish");
   BOOST_TEST(subscriber_pubsub->snapshot().active_handlers == 0U);

   forge::asio::blocking::run(publisher.runtime(), publisher.shutdown());
   forge::asio::blocking::run(subscriber.runtime(), subscriber.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_pubsub_plugin_enforces_topic_policy_and_handler_bounds) {
   auto config = test_p2p_config(test_peer(97));
   config.set("plugins.p2p.pubsub.sign-publishes", false);
   config.set("plugins.p2p.pubsub.max-handlers-per-topic", std::uint64_t{1});
   config.set("plugins.p2p.pubsub.max-message-size", std::uint64_t{4});
   config.set("plugins.p2p.pubsub.allowed-topics",
              forge::config::value::array_type{forge::config::value{"forge.allowed"}});
   config.set("plugins.p2p.pubsub.denied-topics", forge::config::value::array_type{forge::config::value{"forge.denied"}});

   auto app = pubsub_application{};
   app.configure(config);
   forge::asio::blocking::run(app.runtime(), app.startup());

   auto pubsub = app.apis().get<forge::plugins::p2p::pubsub::api>(
      {.id = {"forge.plugins.p2p.pubsub"}, .major = 1, .min_revision = 0});
   auto handler = [](forge::plugins::p2p::pubsub::message) -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
      co_return forge::p2p::pubsub::validation_result::ignore;
   };

   auto subscription = forge::asio::blocking::run(
      app.runtime(), pubsub->subscribe(forge::p2p::pubsub::topic{.value = "forge.allowed"}, handler));
   BOOST_TEST(subscription.id != 0U);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(),
                                              pubsub->subscribe(forge::p2p::pubsub::topic{.value = "forge.allowed"},
                                                                handler)),
                     forge::plugins::p2p::pubsub::exceptions::handler_limit);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(),
                                              pubsub->publish(forge::p2p::pubsub::topic{.value = "forge.denied"}, {1})),
                     forge::plugins::p2p::pubsub::exceptions::topic_not_allowed);
   BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(),
                                              pubsub->publish(forge::p2p::pubsub::topic{.value = "forge.allowed"},
                                                              {1, 2, 3, 4, 5})),
                     forge::plugins::p2p::pubsub::exceptions::message_too_large);

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_plugin_config_is_described_from_public_schema) {
   auto plugin = forge::plugins::p2p::resolver::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "plugins.p2p.resolver");

   const auto& protocol = require_field(*descriptor, "protocol-id");
   BOOST_TEST(protocol.has_default);
   BOOST_TEST(std::get<std::string>(protocol.default_value.storage) == "/forge/api/resolver/1");

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
   auto runtime = forge::asio::runtime{};
   auto plugin = forge::plugins::p2p::resolver::plugin{};
   auto apis = forge::api::registry{};
   auto provider = forge::api::installer{apis};
   forge::asio::blocking::run(runtime, plugin.provide(provider));

   auto resolver = apis.get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});

   auto plan = forge::api::binding().serve(apis).build();
   BOOST_CHECK_THROW(resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/forge/api/node-test/1"}),
                     forge::plugins::p2p::resolver::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW((void)resolver->local_apis(),
                     forge::plugins::p2p::resolver::exceptions::plugin_not_initialized);
   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime, resolver->peer_apis(test_peer(40))),
                     forge::plugins::p2p::resolver::exceptions::plugin_not_initialized);
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_invalid_typed_config_before_startup) {
   {
      auto config = test_p2p_config(test_peer(45));
      config.set("plugins.p2p.resolver.protocol-id", std::string{"forge/api/resolver/1"});
      auto app = resolver_only_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::resolver::exceptions::invalid_config);
   }

   {
      auto config = test_p2p_config(test_peer(46));
      config.set("plugins.p2p.resolver.max-cached-peers", std::uint64_t{0});
      auto app = resolver_only_application{};
      BOOST_CHECK_THROW(app.configure(config), forge::plugins::p2p::resolver::exceptions::invalid_config);
   }
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_publishes_metadata_and_delegates_route_mounting) {
   auto app = resolver_plugin_application{};
   app.configure(test_p2p_config(test_peer(50)));
   forge::asio::blocking::run(app.runtime(), app.initialize());

   auto resolver = app.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   const auto entries = resolver->local_apis();
   BOOST_REQUIRE_EQUAL(entries.size(), 1U);
   BOOST_TEST(entries.front().id.value == "node.test");
   BOOST_TEST(entries.front().version.major == 1U);
   BOOST_TEST(entries.front().version.revision == 0U);
   BOOST_TEST(entries.front().protocol == "/forge/api/node-test/1");
   BOOST_TEST(entries.front().codec.value == "forge.raw");
   BOOST_REQUIRE_EQUAL(entries.front().methods.size(), 1U);
   BOOST_TEST(entries.front().methods.front().name == "ping");
   BOOST_TEST(static_cast<int>(entries.front().methods.front().kind) ==
              static_cast<int>(forge::api::method_kind::unary));

   forge::asio::blocking::run(app.runtime(), app.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_duplicate_api_and_resolver_protocol_conflict) {
   {
      auto app = duplicate_resolver_plugin_application{};
      app.configure(test_p2p_config(test_peer(60)));
      BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.initialize()),
                        forge::plugins::p2p::resolver::exceptions::duplicate_api);
   }

   {
      auto app = resolver_protocol_conflict_application{};
      app.configure(test_p2p_config(test_peer(61)));
      BOOST_CHECK_THROW(forge::asio::blocking::run(app.runtime(), app.initialize()),
                        forge::plugins::p2p::resolver::exceptions::duplicate_api);
   }
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_resolves_remote_api_and_opens_typed_remote) {
   const auto server_peer = test_peer(70);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(71));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   const auto remote_entries = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(remote_entries.size(), 1U);
   BOOST_TEST(remote_entries.front().protocol == "/forge/api/node-test/1");

   auto resolved = forge::asio::blocking::run(
      client.runtime(), resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 0}));
   BOOST_TEST(resolved.api.protocol == "/forge/api/node-test/1");

   auto remote = forge::asio::blocking::run(client.runtime(), resolver->remote<node_test_api>(server_peer));
   const auto response = forge::asio::blocking::run(client.runtime(), remote->ping(41));
   BOOST_TEST(response == 42);

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_remote_honors_advertised_transport_options) {
   const auto server_peer = test_peer(72);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_custom_transport_application{};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(73));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   const auto remote_entries = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(remote_entries.size(), 1U);
   BOOST_TEST(remote_entries.front().codec.value == "forge.test.raw");
   BOOST_TEST(remote_entries.front().max_inflight == 7U);
   BOOST_TEST(remote_entries.front().max_frame_size == 512U * 1024U);

   auto remote = forge::asio::blocking::run(client.runtime(), resolver->remote<node_test_api>(server_peer));
   const auto response = forge::asio::blocking::run(client.runtime(), remote->ping(41));
   BOOST_TEST(response == 42);

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_supports_receipt_based_product_api) {
   const auto server_peer = test_peer(74);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server_state = std::make_shared<receipt_test_state>();
   auto server = receipt_resolver_application{server_state};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(75));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   const auto resolved = forge::asio::blocking::run(
      client.runtime(), resolver->resolve(server_peer, {.id = {"receipt.test"}, .major = 1, .min_revision = 0}));
   BOOST_TEST(resolved.api.protocol == "/forge/api/receipt-test/1");

   auto remote = forge::asio::blocking::run(client.runtime(), resolver->remote<receipt_test_api>(server_peer));
   const auto request =
      operation_request{.request_id = "request-1", .subject = "neutral-operation", .revision = 7};

   const auto first = forge::asio::blocking::run(client.runtime(), remote->apply(request));
   BOOST_TEST(first.accepted);
   BOOST_TEST(first.request_id == request.request_id);
   BOOST_TEST(first.applied_revision == 1U);
   BOOST_TEST(first.authority == "receipt-test");
   BOOST_TEST(first.evidence == "neutral-operation:7:1");

   const auto repeated = forge::asio::blocking::run(client.runtime(), remote->apply(request));
   BOOST_TEST(repeated.request_id == first.request_id);
   BOOST_TEST(repeated.accepted == first.accepted);
   BOOST_TEST(repeated.applied_revision == first.applied_revision);
   BOOST_TEST(repeated.authority == first.authority);
   BOOST_TEST(repeated.evidence == first.evidence);
   {
      auto lock = std::scoped_lock{server_state->mutex};
      BOOST_TEST(server_state->applied == 1U);
      BOOST_TEST(server_state->receipts.size() == 1U);
   }

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_enforces_version_compatibility) {
   const auto server_peer = test_peer(80);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(81));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   BOOST_CHECK_NO_THROW(forge::asio::blocking::run(
      client.runtime(), resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 0})));
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        client.runtime(),
                        resolver->resolve(server_peer, {.id = {"node.test"}, .major = 1, .min_revision = 10})),
                     forge::plugins::p2p::resolver::exceptions::incompatible_api);

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_rejects_malformed_remote_metadata_without_caching_it) {
   const auto server_peer = test_peer(85);
   auto bad = resolver_test_entry("/forge/api/node-test/1");
   auto duplicate = forge::plugins::p2p::resolver::response{.apis = {bad, bad}};
   auto good = forge::plugins::p2p::resolver::response{
      .apis = {resolver_test_entry("/forge/api/node-test/1")},
   };
   auto state = std::make_shared<scripted_resolver_state>(
      scripted_resolver_state{.responses = {std::move(duplicate), std::move(good)}});

   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = scripted_resolver_application{state};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(86));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   BOOST_CHECK_THROW(forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)),
                     forge::plugins::p2p::resolver::exceptions::protocol_error);
   const auto entries = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
   BOOST_REQUIRE_EQUAL(entries.size(), 1U);
   BOOST_TEST(state->calls == 2U);

   forge::asio::blocking::run(client.runtime(), client.shutdown());
   forge::asio::blocking::run(server.runtime(), server.shutdown());
}

BOOST_AUTO_TEST_CASE(p2p_api_resolver_cache_ttl_and_force_refresh_are_behavioral) {
   const auto server_peer = test_peer(90);
   auto server_config = test_p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::value::array_type{
                                      forge::config::value{"/ip4/127.0.0.1/udp/0/quic-v1"}});

   auto server = resolver_plugin_application{};
   server.configure(server_config);
   forge::asio::blocking::run(server.runtime(), server.startup());

   auto server_p2p = server.apis().get<forge::plugins::p2p::node::api>(
      {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
   const auto server_endpoint = server_p2p->local_endpoint();
   BOOST_REQUIRE(server_endpoint.has_value());

   auto client_config = test_p2p_config(test_peer(91));
   client_config.set("plugins.p2p.node.bootstrap",
                     forge::config::value::array_type{forge::config::value{server_endpoint->to_string()}});
   client_config.set("plugins.p2p.resolver.cache-ttl-ms", std::uint64_t{200});
   auto client = resolver_only_application{};
   client.configure(client_config);
   forge::asio::blocking::run(client.runtime(), client.startup());

   auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
      {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
   BOOST_REQUIRE_EQUAL(forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)).size(), 1U);

   forge::asio::blocking::run(server.runtime(), server.shutdown());
   BOOST_REQUIRE_EQUAL(forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)).size(), 1U);

   BOOST_CHECK_THROW(forge::asio::blocking::run(client.runtime(),
                                              resolver->peer_apis(server_peer, {.force_refresh = true})),
                     forge::exceptions::base);

   std::this_thread::sleep_for(std::chrono::milliseconds{250});
   BOOST_CHECK_THROW(forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer)),
                     forge::exceptions::base);

   forge::asio::blocking::run(client.runtime(), client.shutdown());
}
