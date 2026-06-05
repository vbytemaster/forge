# fcl_plugins

`fcl_plugins` contains ready-made infrastructure plugins built on top of
`fcl_app`. These plugins own lifecycle and transport bootstrap, then publish
small typed APIs for product plugins to contribute behavior safely.

## When To Use

- An application wants one shared infrastructure component, such as a P2P node,
  instead of every plugin constructing its own runtime.
- Product plugins need to mount routes, publish typed API protocols or send
  messages without owning transport lifecycle.
- Tests need the same composition shape as production.

## When Not To Use

- Do not put business logic in infrastructure plugins.
- Do not expose raw transport owners when a narrower plugin API is enough.
- Do not register duplicate network routes from many plugins directly against
  the same transport object.

## Public Modules

- `fcl.plugins.p2p_node` — ready P2P node plugin and safe local API.
- `fcl.plugins` — aggregate import.

Target: `fcl_plugins`.

Dependencies: `fcl_app`, `fcl_api`, `fcl_p2p`, `fcl_config`, `fcl_asio`.

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

G.2 will clean the public `p2p_node` contract into a narrow host facade:
`local_peer`, `local_endpoints`, `network_info`, `publish_api`,
`remote<T>(...)` and advanced `publish_protocol`. The current delivery/outbox,
broadcast and raw peer-metrics surface is legacy pre-G.2 scope and must not be
treated as the target API for new product plugins.

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

`allow-insecure-test-mode` exists for local tests only; production config should
provide certificate and private key material.

Register the infrastructure plugin through its owner type:

```cpp
auto builder = fcl::app::application_builder{};
builder.plugin(fcl::plugins::p2p_node::descriptor());
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

For raw product protocols, publish a protocol route instead of an API binding:

```cpp
p2p->publish_protocol(
   {.value = "/product/blob-transfer/1"},
   [](fcl::p2p::node::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
      auto bytes = co_await incoming.stream.async_read_frame();
      co_await handle_blob_frame(incoming.session.remote_peer, std::move(bytes));
   });
```

`publish_protocol(...)` is an inbound route contribution: it runs when a remote
peer opens that `protocol_id`. If the raw protocol needs an ACK or response, the
handler writes it to the incoming stream. Use `fcl_api` over P2P when you want
standard request/response/error handling instead of manual ACK frames.

Outbound product APIs should normally use the typed `remote<T>(...)` helper that
G.2 adds over `fcl.p2p.api` and `fcl.api.transport`. Raw one-shot protocol sends
are an advanced escape hatch for protocols that intentionally own their own
wire format and acknowledgement. Durable retry, outbox storage and
application-level broadcast are planned as separate focused plugins or product
services, not as part of the target `p2p_node` contract.

## Risks And Anti-Patterns

- Do not let product plugins call `node.register_protocol_handler(...)`
  directly when the node is owned by `p2p_node`; publish a route contribution.
- Do not let product plugins override codec, inflight limits or bootstrap policy
  that the node plugin owns from config.
- Do not expose `fcl::p2p::node::open_options`, relay peer selection or hole-punch
  calls to ordinary product plugins. Use semantic `p2p_node::send_options`.
- Do not create one P2P node per product protocol inside the same application.
  One owner plugin should mount multiple bindings/routes.
- Do not use `p2p_node` as product authorization. It only owns transport
  lifecycle and route/API contribution points.
- Do not attach durable delivery, outbox persistence or product broadcast
  semantics to `p2p_node`. If needed, those belong to a future focused
  `p2p_delivery` plugin or a product service.
- Keep AutoNAT, AutoRelay, DHT, rendezvous, pubsub/gossip and relay discovery in
  `fcl_p2p`. `p2p_node` stays lifecycle/config/composition glue over the shared
  network node.
- Keep the host facade narrow. Future helpers such as `p2p_api_catalog`,
  `p2p_diagnostics`, `p2p_pubsub` and optional `p2p_delivery` should be focused
  friend plugins that compose through the safe APIs exposed by `p2p_node`.

## Tests

`test_fcl_plugins` covers publishing the safe local P2P node API, contributing
typed API and raw protocol routes before startup, deterministic duplicate
protocol rejection and plugin exceptions. Pre-G.2 delivery/outbox tests are
legacy coverage until the cleanup removes that surface or moves it into a
focused delivery plugin.
