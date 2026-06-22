# fcl_plugins

`fcl_plugins` contains ready-made infrastructure plugins built on top of
`fcl_app`. These plugins own lifecycle and transport bootstrap, then publish
small typed APIs for application plugins to contribute behavior safely.

## When To Use

- An application wants one shared infrastructure component, such as a P2P node,
  instead of every plugin constructing its own runtime.
- Application plugins need to mount routes, publish typed API protocols or open
  typed remote APIs without owning transport lifecycle.
- Tests need the same composition shape as deployed applications.

## When Not To Use

- Do not put business logic in infrastructure plugins.
- Do not expose raw transport owners when a narrower plugin API is enough.
- Do not register duplicate network routes from many plugins directly against
  the same transport object.

## Public Modules

Each official plugin exposes focused slice modules:

- `fcl.plugins.<family>.<name>.plugin` — lifecycle object and `descriptor()`.
- `fcl.plugins.<family>.<name>.api` — typed local/remote API contracts.
- `fcl.plugins.<family>.<name>.types` — config, options and value DTOs.
- `fcl.plugins.<family>.<name>.exceptions` — typed exception category and aliases.

Available plugin namespace owners are `fcl::plugins::http::server`,
`fcl::plugins::p2p::node`, `fcl::plugins::p2p::resolver`,
`fcl::plugins::p2p::diagnostics`, `fcl::plugins::p2p::pubsub` and
`fcl::plugins::signing::provider`.

Do not import a root or per-plugin aggregate module; those files are not part of
the public module surface. Use the explicit slice module and link the matching
plugin target. The `fcl_plugins` target and `plugins` package component remain
aggregate CMake conveniences.

Each focused plugin uses a namespace owner. The lifecycle object is named
`plugin`, public contracts live beside it, and registration uses the
namespace-level `descriptor()`:

```cpp
registry.register_plugin(fcl::plugins::p2p::node::descriptor());
auto direct = fcl::plugins::signing::provider::plugin{};
```

Aggregate target: `fcl_plugins`.

Focused targets: `fcl_plugins_http_server`, `fcl_plugins_signing_provider`,
`fcl_plugins_p2p_node`, `fcl_plugins_p2p_resolver`,
`fcl_plugins_p2p_diagnostics` and `fcl_plugins_p2p_pubsub`.

Dependencies are intentionally narrow per plugin.
`fcl::plugins::signing::provider` does not depend on P2P or HTTP;
`fcl::plugins::http::server` depends on the HTTP library and app/API
composition, while `fcl::plugins::p2p::resolver`,
`fcl::plugins::p2p::diagnostics` and `fcl::plugins::p2p::pubsub` compose
through `fcl_plugins_p2p_node`.

## HTTP Server Plugin

`fcl::plugins::http::server` owns one configured `fcl_http::server` lifecycle
and exposes a local-only typed publish API. Application plugins contribute
`FCL_HTTP_API` interfaces through `publish<Interface>()` and middleware before
startup; the server plugin mounts them under the configured or per-publish
base path and then starts the HTTP server. Direct `fcl::http::router` or
`fcl::http::api::binding_plan` mounting belongs to applications that intentionally own
the low-level `fcl_http` server, not to the official plugin surface.

Config section `plugins.http.server` is schema-driven:

```yaml
plugins.http.server:
  bind-address: 127.0.0.1
  port: 8080
  api-base-path: /api/v1
  max-request-body-bytes: 16777216
  max-header-bytes: 65536
  read-timeout-ms: 30000
  idle-timeout-ms: 120000
```

Typed API publication is the primary path:

```cpp
import fcl.plugins.http.server.api;
import fcl.plugins.http.server.middleware;
import fcl.plugins.http.server.plugin;

class cache_publisher final : public fcl::app::plugin {
 public:
   boost::asio::awaitable<void>
   initialize(fcl::app::plugin_context& context) override {
      auto http = context.apis().get<fcl::plugins::http::server::api>(
         fcl::plugins::http::server::api::ref());

      co_await http->publish<cache_api>(
         fcl::plugins::http::server::publish_options{.base_path = "/api/v1"});
   }
};
```

Middleware uses plugin-owned typed request/response wrappers. The bridge to
`fcl_http` router internals stays private to the plugin:

```cpp
co_await http->use(fcl::plugins::http::server::middleware_descriptor{
   .id = "auth",
   .phase = fcl::plugins::http::server::middleware_phase::security,
   .order = 10,
   .path_prefix = "/api",
   .handler = [](const fcl::plugins::http::server::middleware_request& request,
                 fcl::plugins::http::server::middleware_next next)
      -> boost::asio::awaitable<fcl::plugins::http::server::middleware_response> {
      if (!request.header("Authorization").has_value()) {
         co_return fcl::plugins::http::server::middleware_response::text(
            fcl::http::status::unauthorized, "missing authorization");
      }
      co_return co_await next();
   },
});
```

Publishing after server startup throws `publication_closed`. The plugin does not
expose raw `get`/`post` route mutation, raw `fcl::http::middleware_descriptor`,
diagnostics/status APIs, file/upload publishers, TLS/auth/CORS policy or S3
semantics.

## Signature Provider Plugin

`fcl::plugins::signing::provider` publishes a local-only API for producing digital
signatures from configured private keys. It is intentionally not tied to a P2P
node: application plugins can use it for local receipts, protocol
authentication or other signature-producing flows without owning key parsing or
text-profile formatting.

The plugin is not a wallet, vault, hardware security module or key-management
service. It does not fetch keys from remote systems and it does not authorize
what a signed payload means. It only enforces configured key ids, allowed
purposes, required algorithms and output profiles.

Config section `plugins.signing.provider` owns local key material. The structured
`keys` field is decoded through nested `fcl_schema` rules, remains
secret/redacted as one object-list field, and is not accepted through generated
CLI or environment-variable helpers; load it from a protected config source
before handing the document to the application.

```yaml
plugins.signing.provider:
  default-output-profile: fcl
  keys:
    - id: service-key
      private-key: "<redacted private key>"
      input-profile: fcl
      purposes: ["api.receipt"]
```

```cpp
registry.register_plugin(fcl::plugins::signing::provider::descriptor());
```

```cpp
auto signatures = context.apis().get<fcl::plugins::signing::provider::api>(
   {.id = {"fcl.plugins.signing.provider"}, .major = 1});

auto result = co_await signatures->sign(
   fcl::plugins::signing::provider::request{
      .key_id = "service-key",
      .purpose = "api.receipt",
      .digest = digest,
      .required_algorithm =
         fcl::plugins::signing::provider::key_algorithm::secp256k1,
      .output_profile = "fcl",
   });
```

## P2P Node Plugin

`fcl::plugins::p2p::node` starts and stops one `fcl::p2p::node` through the
normal application lifecycle. It publishes `fcl::plugins::p2p::node::api`
through `fcl_api` so other plugins can contribute protocol handlers and API
bindings before startup.

AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip, relay-only path support,
endpoint/address compatibility, peer discovery and libp2p protocol mechanics
belong to `fcl_p2p`. `fcl::plugins::p2p::node` is not a private P2P network
stack; it only maps config into the shared node and mounts route/API
contributions.

The long-term network profile is libp2p-compatible, but the plugin boundary
does not change: `fcl::plugins::p2p::node` enables and configures the shared
FCL node. It must not reimplement Identify, Ping, relay discovery, DHT, pubsub
or interop logic in plugin-local loops.

The public `fcl::plugins::p2p::node` contract is a host facade: `local_peer`,
`local_endpoint`, `local_endpoints`, `network_info`, `publish_api`,
`remote<T>(...)` and advanced `publish_protocol`. Durable message queues,
application fan-out and read-only network diagnostics belong to focused plugins
or application services, not to this host facade.

`fcl::plugins::p2p::node::config` is the public config contract. Config section
`plugins.p2p.node` owns transport bootstrap and policy:

`fcl::plugins::p2p::node` exceptions are part of the same module and live under
`fcl::plugins::p2p::node::exceptions::*`. `fcl_plugins` does not provide a
single shared plugin-exception family because each infrastructure plugin owns
its own public failure vocabulary.

```yaml
plugins.p2p.node:
  listen: ["/ip4/0.0.0.0/udp/9443/quic-v1"]
  bootstrap: ["/dns4/node-a.example/udp/9443/quic-v1"]
  advertised-endpoints: ["/dns4/node-b.example/udp/9443/quic-v1"]
  certificate-pem: "..."
  private-key-pem: "..."
  api-codec: fcl.raw
  api:
    deadline-ms: 5000
    max-frame-size: 16777216
  max-inflight-per-peer: 64
  path:
    policy: direct-preferred
  relay:
    trust: known-only
    client-enabled: true
    server-enabled: false
    public-allowed: false
    reservation-ttl-ms: 60000
    max-candidates: 4
```

`allow-insecure-test-mode` exists for local tests only; deployed config should
provide certificate and private key material.

Register the infrastructure plugin through its owner namespace:

```cpp
registry.register_plugin(fcl::plugins::p2p::node::descriptor());
```

```cpp
class cache_routes final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return {.value = "cache.routes"};
   }

   [[nodiscard]] std::string version() const override {
      return "1.0.0";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p::node::api>(
         {.id = {"fcl.plugins.p2p.node"}, .major = 1});

      auto plan = fcl::api::binding()
         .serve(context.apis())
         .export_api<cache_api>({.id = {"cache"}, .major = 1, .min_revision = 8})
         .build();

      p2p->publish_api(std::move(plan), {.value = "/fcl/api/cache/1"});
      co_return;
   }
};
```

For raw application protocols, publish a protocol route instead of an API binding:

```cpp
p2p->publish_protocol(
   {.value = "/fcl/test/blob-transfer/1"},
   [](fcl::p2p::node::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
      auto bytes = co_await incoming.stream.async_read_frame();
      co_await handle_blob_frame(incoming.session.remote_peer, std::move(bytes));
   });
```

`publish_protocol(...)` is an inbound route contribution: it runs when a remote
peer opens that `protocol_id`. If the raw protocol needs an ACK or response, the
handler writes it to the incoming stream. Use `fcl_api` over P2P when you want
standard request/response/error handling instead of manual ACK frames.

Outbound application APIs should normally use the typed `remote<T>(...)` helper over
`fcl.p2p.api` and `fcl.transport.api`:

```cpp
auto cache = co_await p2p->remote<cache_api>(
   peer,
   {.value = "/fcl/api/cache/1"},
   fcl::plugins::p2p::node::remote_options{.open_deadline = 5s});

auto reply = co_await cache->read(request);
```

Raw application protocols remain an advanced escape hatch for protocols that
intentionally own their own wire format and acknowledgement. Durable retry,
store-backed queues and application-level fan-out are planned as separate
focused plugins or application services, not as part of `fcl::plugins::p2p::node`.

## P2P API Resolver Plugin

`fcl::plugins::p2p::resolver` is the focused discovery layer for API-over-P2P metadata. An
application plugin publishes a discoverable API through the resolver; the resolver
delegates the actual route mount to `fcl::plugins::p2p::node`, records a bounded serializable
projection of the API descriptor and serves that projection over the private
FCL protocol `/fcl/api/resolver/1`.

The resolver does not own P2P discovery, authorization, DHT, rendezvous, relay
policy or application routing decisions. Peer identity comes from the authenticated
P2P session. Resolver payloads are metadata only and cannot claim or override
the remote peer id.

```yaml
plugins.p2p.resolver:
  protocol-id: /fcl/api/resolver/1
  cache-ttl-ms: 60000
  query-deadline-ms: 5000
  open-deadline-ms: 10000
  max-cached-peers: 4096
  max-apis-per-peer: 1024
  max-methods-per-api: 256
  max-errors-per-method: 64
```

Publish a discoverable API from an application plugin:

```cpp
auto resolver = context.apis().get<fcl::plugins::p2p::resolver::api>(
   {.id = {"fcl.plugins.p2p.resolver"}, .major = 1});

auto plan = fcl::api::binding()
   .serve(context.apis())
   .export_api<cache_api>({.id = {"cache"}, .major = 1, .min_revision = 8})
   .build();

resolver->publish_api(std::move(plan), {.value = "/fcl/api/cache/1"});
```

Open a compatible remote API without hardcoding that application protocol id:

```cpp
auto cache = co_await resolver->remote<cache_api>(
   peer,
   fcl::plugins::p2p::resolver::resolve_options{
      .open_deadline = 5s,
   });

auto reply = co_await cache->read(request);
```

The wire response is not raw `fcl::api::descriptor`: descriptors contain local
runtime function/type metadata. The resolver sends only the stable projection:
API id/version, protocol id string, codec, limits, method names/kinds and error
identities.

### API Receipt Pattern

Consumers that need proof of an operation result should model it as an ordinary
typed API request returning a domain receipt. The receipt is application-level
evidence; it is not a generic P2P delivery acknowledgement. The request should
carry an idempotency key so retries can return the same receipt without
executing the operation twice.

```cpp
struct apply_request {
   std::string request_id; // Idempotency key owned by this API contract.
   std::string subject;
   std::uint64_t revision = 0;
};

struct apply_receipt {
   std::string request_id;
   bool accepted = false;
   std::uint64_t applied_revision = 0;
   std::string authority;
   std::string evidence;
};

class operation_api
   : public fcl::api::contract<
        operation_api,
        fcl::api::surface::local | fcl::api::surface::remote> {
 public:
   virtual ~operation_api() = default;
   virtual boost::asio::awaitable<apply_receipt> apply(apply_request request) = 0;
};

FCL_API(operation_api, FCL_API_CONTRACT("operation", 1, 0), FCL_API_METHOD(apply))
```

Server plugins publish this API through `fcl::plugins::p2p::resolver`; client plugins open
it with `resolver->remote<operation_api>(peer)` and call the typed method. The
consumer remains responsible for authorization, durable state and authoritative
business semantics. A durable delivery plugin would be a separate optional layer
for asynchronous store-backed retry, not a prerequisite for request/receipt
APIs.

## P2P Diagnostics Plugin

`fcl::plugins::p2p::diagnostics` is the focused read-only visibility layer for the shared
`fcl::plugins::p2p::node`. It exposes immutable snapshots from `fcl_p2p`: local network
identity, endpoints, metrics, resource scopes, peer records, sessions, relay
reservations, connection protection/pruning state and pubsub counters.

It does not add a network diagnostics protocol, remote operator access,
authorization, remediation, retries or routing policy. Application code can inspect
health through this plugin, but `fcl_p2p` remains the owner of real network
state and `fcl::plugins::p2p::node` remains the lifecycle/route facade.

```yaml
plugins.p2p.diagnostics:
  max-peers: 1024
  max-sessions: 1024
  max-endpoints-per-peer: 64
  max-protocols-per-peer: 128
  max-relay-reservations-per-peer: 64
```

```cpp
auto diagnostics = context.apis().get<fcl::plugins::p2p::diagnostics::api>(
   {.id = {"fcl.plugins.p2p.diagnostics"}, .major = 1});

auto network = diagnostics->network();
auto resources = diagnostics->resources();
auto peer = diagnostics->peer(remote_peer);
```

Snapshot limits are deterministic truncation controls for operator and test
surfaces. They are not resource-management policy and do not change the running
node.

## P2P PubSub Plugin

`fcl::plugins::p2p::pubsub` is the focused application facade over core `fcl_p2p` GossipSub.
It lets plugins publish and subscribe to topics without owning mesh, scoring,
heartbeat, protocol negotiation or wire compatibility. Those mechanics remain
inside `fcl_p2p`.

The plugin supports raw byte messages, typed `fcl.raw` payload helpers, bounded
local handler fan-out, deterministic subscription ids, handler deadlines,
topic allow/deny policy and a local snapshot. It is not a durable queue, not an
exactly-once delivery system and not an application authorization layer.

```yaml
plugins.p2p.pubsub:
  max-topics: 1024
  max-handlers-per-topic: 64
  max-active-handlers: 4096
  max-message-size: 1048576
  handler-deadline-ms: 5000
  allowed-topics: []
  denied-topics: []
  sign-publishes: true
```

```cpp
auto pubsub = context.apis().get<fcl::plugins::p2p::pubsub::api>(
   {.id = {"fcl.plugins.p2p.pubsub"}, .major = 1});

auto subscription = co_await pubsub->subscribe<cache_event>(
   {.value = "example.cache.events"},
   [](fcl::plugins::p2p::pubsub::typed_message<cache_event> message)
      -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
      co_await handle_cache_event(message.source, message.value);
      co_return fcl::p2p::pubsub::validation_result::accept;
   });

co_await pubsub->publish(
   {.value = "example.cache.events"},
   cache_event{.key = "abc", .revision = 42});
```

## Risks And Anti-Patterns

- Do not let application plugins call `node.register_protocol_handler(...)`
  directly when the node is owned by `fcl::plugins::p2p::node`; publish a route contribution.
- Do not let application plugins override codec, inflight limits or bootstrap policy
  that the node plugin owns from config.
- Do not expose `fcl::p2p::node::open_options`, relay peer selection or
  hole-punch calls to ordinary application plugins. Use typed `remote<T>(...)`
  or a focused plugin facade.
- Do not create one P2P node per application protocol inside the same application.
  One owner plugin should mount multiple bindings/routes.
- Do not use `fcl::plugins::p2p::node` as application authorization. It only owns transport
  lifecycle and route/API contribution points.
- Do not attach durable queue persistence or application fan-out semantics to
  `fcl::plugins::p2p::node`. If needed, those belong to a future focused plugin or application
  service.
- Keep AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip and relay discovery in
  `fcl_p2p`. `fcl::plugins::p2p::node` stays lifecycle/config/composition glue over the shared
  network node.
- Keep the host facade narrow. Focused helpers such as `fcl::plugins::p2p::resolver`,
  `fcl::plugins::p2p::diagnostics`, `fcl::plugins::p2p::pubsub` and any optional durable queue plugin should
  compose through the safe APIs exposed by `fcl::plugins::p2p::node`.

## Tests

`test_fcl_plugins` covers publishing the safe local P2P node API, contributing
typed API and raw protocol routes before startup, deterministic duplicate
protocol rejection, multi-listen endpoint reporting, typed remote API calls and
plugin exceptions. It also covers resolver publication, duplicate API
rejection, compatible remote resolution, malformed metadata rejection, cache TTL
and forced refresh behavior.
