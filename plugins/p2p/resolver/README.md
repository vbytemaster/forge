# P2P Resolver Plugin

`fcl::plugins::p2p::resolver` publishes and resolves typed API metadata over
the shared P2P node. It lets consumers ask a peer which API versions and method
contracts it exposes before opening a typed API connection.

## Identity

- Target: `fcl_plugins_p2p_resolver`
- Package component: `plugins_p2p_resolver`
- Plugin id: `fcl.plugins.p2p.resolver`
- Main API id: `fcl.plugins.p2p.resolver`
- Extra protocol API id: `fcl.plugins.p2p.resolver.protocol`
- Config section: `plugins.p2p.resolver`
- Depends on plugin id: `fcl.plugins.p2p.node`
- Public modules:
  - `fcl.plugins.p2p.resolver.plugin`
  - `fcl.plugins.p2p.resolver.api`
  - `fcl.plugins.p2p.resolver.types`
  - `fcl.plugins.p2p.resolver.exceptions`

## What It Provides

- Publishes local API descriptors under a P2P protocol id.
- Queries peer API descriptors with bounded response validation.
- Resolves a requested `fcl::api::api_ref` to a concrete peer descriptor.
- Opens typed remote API handles with compatibility projection.

It does not own the P2P node lifecycle; it composes through
`fcl.plugins.p2p.node`.

## Config

```yaml
plugins:
   p2p:
      resolver:
         protocol-id: /fcl/api/resolver/1
         cache-ttl-ms: 60000
         query-deadline-ms: 5000
         open-deadline-ms: 10000
         max-cached-peers: 4096
         max-apis-per-peer: 1024
         max-methods-per-api: 256
         max-errors-per-method: 64
```

## Example

```cpp
import fcl.plugins.p2p.resolver.api;
import fcl.plugins.p2p.resolver.plugin;

class catalog_resolver_plugin final : public fcl::app::plugin {
 public:
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto resolver = context.apis().get<fcl::plugins::p2p::resolver::api>(
         {.id = {"fcl.plugins.p2p.resolver"}, .major = 1});

      auto plan = fcl::api::binding()
         .serve(context.apis())
         .export_api<catalog_api>()
         .build();

      resolver->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/catalog/api/1"});
      co_return;
   }
};
```

```cpp
auto catalog = co_await resolver->remote<catalog_api>(peer);
auto value = co_await catalog->read(request);
```
