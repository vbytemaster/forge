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

`fcl::plugins::p2p_node::config` is the public config contract. Config section
`p2p` owns transport bootstrap and policy:

```yaml
p2p:
  listen: ["quic://0.0.0.0:9443"]
  bootstrap: ["quic://node-a.example:9443"]
  advertised-endpoints: ["quic://node-b.example:9443"]
  certificate-pem: "..."
  private-key-pem: "..."
  api-codec: fcl.raw
  max-inflight-per-peer: 64
```

`allow-insecure-test-mode` exists for local tests only; production config should
provide certificate and private key material.

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
   [](fcl::p2p::incoming_protocol_stream incoming) -> boost::asio::awaitable<void> {
      auto bytes = co_await incoming.stream.async_read_frame();
      co_await handle_blob_frame(incoming.session.remote_peer, std::move(bytes));
   });
```

For outbound product messages, use `fcl::p2p::message`:

```cpp
auto message = fcl::p2p::message{
   fcl::p2p::protocol_id{.value = "/product/cache/announce/1"},
   protocol::announce_chunk{.ref = ref}};

co_await p2p->broadcast(std::move(message));
```

## Risks And Anti-Patterns

- Do not let product plugins call `node.register_protocol_handler(...)`
  directly when the node is owned by `p2p_node`; publish a route contribution.
- Do not let product plugins override codec, inflight limits or bootstrap policy
  that the node plugin owns from config.
- Do not create one P2P node per product protocol inside the same application.
  One owner plugin should mount multiple bindings/routes.
- Do not use `p2p_node` as product authorization. It only owns transport
  lifecycle and route/API contribution points.

## Tests

`test_fcl_plugins` covers publishing the safe local P2P node API, contributing
typed API and raw protocol routes before startup, and deterministic duplicate
protocol rejection.
