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

- `fcl.plugins.p2p_node` — ready P2P node plugin and safe local API.
- `fcl.plugins.p2p_api_resolver` — API-over-P2P metadata resolver plugin.
- `fcl.plugins.p2p_diagnostics` — read-only P2P host diagnostics plugin.
- `fcl.plugins` — aggregate import.

Target: `fcl_plugins`.

Dependencies: `fcl_app`, `fcl_api`, `fcl_api_transport`, `fcl_p2p`,
`fcl_config`, `fcl_asio`.

## P2P Node Plugin

`p2p_node` starts and stops one `fcl::p2p::node` through the normal application
lifecycle. It publishes `fcl::plugins::p2p_node::api` through `fcl_api` so other
plugins can contribute protocol handlers and API bindings before startup.

AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip, relay-only path support,
endpoint/address compatibility, peer discovery and libp2p protocol mechanics
belong to `fcl_p2p`. `p2p_node` is not a private P2P network stack; it only maps
config into the shared node and mounts route/API contributions.

The long-term network profile is libp2p-compatible, but the plugin boundary
does not change: `p2p_node` enables and configures the shared FCL node. It must
not reimplement Identify, Ping, relay discovery, DHT, pubsub or interop logic in
plugin-local loops.

G.2 narrows the public `p2p_node` contract to a host facade: `local_peer`,
`local_endpoint`, `local_endpoints`, `network_info`, `publish_api`,
`remote<T>(...)` and advanced `publish_protocol`. Durable message queues,
application fan-out and read-only network diagnostics belong to focused
plugins or application services, not to this host facade.

`fcl::plugins::p2p_node::config` is the public config contract. Config section
`p2p` owns transport bootstrap and policy:

`p2p_node` exceptions are part of the same module and live under
`fcl::plugins::p2p_node::exceptions::*`. `fcl_plugins` does not provide a
single shared plugin-exception family because each infrastructure plugin owns
its own public failure vocabulary.

```yaml
p2p:
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

Register the infrastructure plugin through its owner type:

```cpp
registry.register_plugin(fcl::plugins::p2p_node::descriptor());
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
      auto p2p = context.apis().get<fcl::plugins::p2p_node::api>(
         {.id = {"fcl.plugins.p2p_node"}, .major = 1});

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
`fcl.p2p.api` and `fcl.api.transport`:

```cpp
auto cache = co_await p2p->remote<cache_api>(
   peer,
   {.value = "/fcl/api/cache/1"},
   fcl::plugins::p2p_node::remote_options{.open_deadline = 5s});

auto reply = co_await cache.call<read_request, read_response>(
   {.id = {"cache"}, .major = 1, .min_revision = 8},
   "read",
   request);
```

Raw application protocols remain an advanced escape hatch for protocols that
intentionally own their own wire format and acknowledgement. Durable retry,
store-backed queues and application-level fan-out are planned as separate
focused plugins or application services, not as part of `p2p_node`.

## P2P API Resolver Plugin

`p2p_api_resolver` is the focused discovery layer for API-over-P2P metadata. A
application plugin publishes a discoverable API through the resolver; the resolver
delegates the actual route mount to `p2p_node`, records a bounded serializable
projection of the API descriptor and serves that projection over the private
FCL protocol `/fcl/api/resolver/1`.

The resolver does not own P2P discovery, authorization, DHT, rendezvous, relay
policy or application routing decisions. Peer identity comes from the authenticated
P2P session. Resolver payloads are metadata only and cannot claim or override
the remote peer id.

```yaml
p2p-api-resolver:
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
auto resolver = context.apis().get<fcl::plugins::p2p_api_resolver::api>(
   {.id = {"fcl.plugins.p2p_api_resolver"}, .major = 1});

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
   fcl::plugins::p2p_api_resolver::resolve_options{
      .open_deadline = 5s,
   });

auto reply = co_await cache.call<read_request, read_response>(
   {.id = {"cache"}, .major = 1, .min_revision = 8},
   "read",
   request);
```

The wire response is not raw `fcl::api::descriptor`: descriptors contain local
runtime function/type metadata. The resolver sends only the stable projection:
API id/version, protocol id string, codec, limits, method names/kinds and error
identities.

## P2P Diagnostics Plugin

`p2p_diagnostics` is the focused read-only visibility layer for the shared
`p2p_node`. It exposes immutable snapshots from `fcl_p2p`: local network
identity, endpoints, metrics, resource scopes, peer records, sessions, relay
reservations, connection protection/pruning state and pubsub counters.

It does not add a network diagnostics protocol, remote operator access,
authorization, remediation, retries or routing policy. Application code can inspect
health through this plugin, but `fcl_p2p` remains the owner of real network
state and `p2p_node` remains the lifecycle/route facade.

```yaml
p2p-diagnostics:
  max-peers: 1024
  max-sessions: 1024
  max-endpoints-per-peer: 64
  max-protocols-per-peer: 128
  max-relay-reservations-per-peer: 64
```

```cpp
auto diagnostics = context.apis().get<fcl::plugins::p2p_diagnostics::api>(
   {.id = {"fcl.plugins.p2p_diagnostics"}, .major = 1});

auto network = diagnostics->network();
auto resources = diagnostics->resources();
auto peer = diagnostics->peer(remote_peer);
```

Snapshot limits are deterministic truncation controls for operator and test
surfaces. They are not resource-management policy and do not change the running
node.

## Risks And Anti-Patterns

- Do not let application plugins call `node.register_protocol_handler(...)`
  directly when the node is owned by `p2p_node`; publish a route contribution.
- Do not let application plugins override codec, inflight limits or bootstrap policy
  that the node plugin owns from config.
- Do not expose `fcl::p2p::node::open_options`, relay peer selection or hole-punch
  calls to ordinary application plugins. Use typed `remote<T>(...)` or a focused
  future plugin facade.
- Do not create one P2P node per application protocol inside the same application.
  One owner plugin should mount multiple bindings/routes.
- Do not use `p2p_node` as application authorization. It only owns transport
  lifecycle and route/API contribution points.
- Do not attach durable queue persistence or application fan-out semantics to
  `p2p_node`. If needed, those belong to a future focused plugin or application
  service.
- Keep AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip and relay discovery in
  `fcl_p2p`. `p2p_node` stays lifecycle/config/composition glue over the shared
  network node.
- Keep the host facade narrow. Focused helpers such as `p2p_api_resolver`,
  `p2p_diagnostics`, `p2p_pubsub` and an optional durable queue plugin should be focused
  friend plugins that compose through the safe APIs exposed by `p2p_node`.

## Tests

`test_fcl_plugins` covers publishing the safe local P2P node API, contributing
typed API and raw protocol routes before startup, deterministic duplicate
protocol rejection, multi-listen endpoint reporting, typed remote API calls and
plugin exceptions. It also covers resolver publication, duplicate API
rejection, compatible remote resolution, malformed metadata rejection, cache TTL
and forced refresh behavior.
