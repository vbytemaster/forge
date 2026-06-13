module;

#include <boost/describe.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <vector>

export module fcl.plugins.p2p_api_resolver.types;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.api.transport.exceptions;
import fcl.api.transport.options;
import fcl.api.transport.client;
import fcl.api.transport.connection;
import fcl.api.transport.server;
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.identify;
import fcl.p2p.diagnostics;
import fcl.p2p.discovery;
import fcl.p2p.dht;
import fcl.p2p.rendezvous;
import fcl.p2p.pubsub;
import fcl.p2p.reachability;
import fcl.p2p.hole_punch;
import fcl.p2p.protocol;
import fcl.p2p.message;
import fcl.p2p.scoring;
import fcl.p2p.relay;
import fcl.p2p.resource_manager;
import fcl.p2p.stream;
import fcl.p2p.negotiation;
import fcl.p2p.peer_store;
import fcl.p2p.node;
import fcl.p2p.api;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::plugins::p2p_api_resolver {

struct config {
   std::string protocol_id = "/fcl/api/resolver/1";
   std::uint64_t cache_ttl_ms = 60'000;
   std::uint64_t query_deadline_ms = 5'000;
   std::uint64_t open_deadline_ms = 10'000;
   std::uint64_t max_cached_peers = 4'096;
   std::uint64_t max_apis_per_peer = 1'024;
   std::uint64_t max_methods_per_api = 256;
   std::uint64_t max_errors_per_method = 64;
};

struct publish_options {
   fcl::api::transport::options transport{};
};

struct resolve_options {
   std::chrono::milliseconds query_deadline{0};
   std::chrono::milliseconds open_deadline{0};
   bool force_refresh = false;
};

struct error {
   std::string name;
   fcl::api::error_identity identity;
   fcl::api::status status_code = fcl::api::status::internal;
   bool retryable = false;

   bool operator==(const error&) const = default;
};

struct method {
   std::string name;
   fcl::api::method_kind kind = fcl::api::method_kind::unary;
   std::vector<error> errors;

   bool operator==(const method&) const = default;
};

struct entry {
   fcl::api::api_id id;
   fcl::api::api_version version;
   std::string protocol;
   fcl::api::codec_id codec{.value = "fcl.raw"};
   std::uint64_t max_inflight = 0;
   std::uint64_t max_frame_size = 0;
   std::vector<method> methods;

   bool operator==(const entry&) const = default;
};

struct resolution {
   entry api;

   bool operator==(const resolution&) const = default;
};

struct resolved_connection {
   fcl::api::transport::connection connection;
   fcl::api::api_ref selected;
};

struct query {
   std::vector<fcl::api::api_ref> apis;

   bool operator==(const query&) const = default;
};

struct response {
   std::vector<entry> apis;

   bool operator==(const response&) const = default;
};

BOOST_DESCRIBE_STRUCT(config, (),
                      (protocol_id, cache_ttl_ms, query_deadline_ms, open_deadline_ms, max_cached_peers,
                       max_apis_per_peer, max_methods_per_api, max_errors_per_method))
BOOST_DESCRIBE_STRUCT(error, (), (name, identity, status_code, retryable))
BOOST_DESCRIBE_STRUCT(method, (), (name, kind, errors))
BOOST_DESCRIBE_STRUCT(entry, (), (id, version, protocol, codec, max_inflight, max_frame_size, methods))
BOOST_DESCRIBE_STRUCT(query, (), (apis))
BOOST_DESCRIBE_STRUCT(response, (), (apis))

} // namespace fcl::plugins::p2p_api_resolver

export template <> struct fcl::schema::rules<fcl::plugins::p2p_api_resolver::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_api_resolver::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_api_resolver::config>();
      schema.field<&fcl::plugins::p2p_api_resolver::config::protocol_id>("protocol-id")
         .default_value("/fcl/api/resolver/1")
         .description("P2P protocol id used for FCL API metadata resolution");
      schema.field<&fcl::plugins::p2p_api_resolver::config::cache_ttl_ms>("cache-ttl-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::query_deadline_ms>("query-deadline-ms")
         .default_value(std::uint64_t{5'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::open_deadline_ms>("open-deadline-ms")
         .default_value(std::uint64_t{10'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_cached_peers>("max-cached-peers")
         .default_value(std::uint64_t{4'096})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_apis_per_peer>("max-apis-per-peer")
         .default_value(std::uint64_t{1'024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_methods_per_api>("max-methods-per-api")
         .default_value(std::uint64_t{256})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_errors_per_method>("max-errors-per-method")
         .default_value(std::uint64_t{64})
         .range(0, 1'000'000);
      return schema;
   }
};
