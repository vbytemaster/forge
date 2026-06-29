# P2P Resolver Plugin

`forge::plugins::p2p::resolver` publishes and resolves typed API metadata over
the shared P2P node. It lets consumers ask a peer which API versions and method
contracts it exposes before opening a typed API connection.

## When To Use

- Peers need to discover which typed Forge APIs another peer exposes.
- A product plugin wants `remote<T>()` handles without hardcoding peer-side API
  descriptors.
- API compatibility should be checked before opening a remote typed connection.

## When Not To Use

- Do not use the resolver for peer discovery, routing, relay selection or
  durable service registry policy.
- Do not use it as an authorization check. It reports capabilities, not trust.
- Do not bypass `forge.plugins.p2p.node`; this plugin depends on the shared
  node.

## Identity

- Target: `forge_plugins_p2p_resolver`
- Package component: `plugins_p2p_resolver`
- Plugin id: `forge.plugins.p2p.resolver`
- Main API id: `forge.plugins.p2p.resolver`
- Extra protocol API id: `forge.plugins.p2p.resolver.protocol`
- Config section: `plugins.p2p.resolver`
- Depends on plugin id: `forge.plugins.p2p.node`
- Public modules:
  - `forge.plugins.p2p.resolver.plugin`
  - `forge.plugins.p2p.resolver.api`
  - `forge.plugins.p2p.resolver.types`
  - `forge.plugins.p2p.resolver.exceptions`

## What It Provides

- Publishes local API descriptors under a P2P protocol id.
- Queries peer API descriptors with bounded response validation.
- Resolves a requested `forge::api::api_ref` to a concrete peer descriptor.
- Opens typed remote API handles with compatibility projection.

It does not own the P2P node lifecycle; it composes through
`forge.plugins.p2p.node`.

## Config

```yaml
plugins:
   p2p:
      resolver:
         protocol-id: /forge/api/resolver/1
         cache-ttl-ms: 60000
         query-deadline-ms: 5000
         open-deadline-ms: 10000
         max-cached-peers: 4096
         max-apis-per-peer: 1024
         max-methods-per-api: 256
         max-errors-per-method: 64
```

## Dependencies

- `forge_app`
- `forge_api`
- `forge_p2p`
- `forge_plugins_p2p_node`
- `forge_config`
- `forge_schema`

## Examples

### Publish And Resolve A Typed API

```cpp
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.plugin;

class catalog_resolver_plugin final : public forge::app::plugin {
 public:
   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
         {.id = {"forge.plugins.p2p.resolver"}, .major = 1});

      auto plan = forge::api::binding()
         .serve(context.apis())
         .export_api<catalog_api>()
         .build();

      resolver->publish_api(std::move(plan), forge::p2p::protocol_id{.value = "/catalog/api/1"});
      co_return;
   }
};
```

```cpp
auto catalog = co_await resolver->remote<catalog_api>(peer);
auto value = co_await catalog->read(request);
```

## Security And Boundaries

- Descriptor data is bounded by config limits before it is accepted.
- The resolver projects API compatibility. Caller trust, admission and
  authorization remain product policy.
- Remote errors should be surfaced as typed API errors, not as raw transport
  messages.

## Common Mistakes

- Treating a successful descriptor query as proof that the peer is trusted.
- Publishing every local API by default. Export only the intended remote
  contract.
- Making cache TTLs unbounded and hiding stale descriptors.

## Tests

- `test_forge_quic_p2p`
- `test_forge_plugins`
