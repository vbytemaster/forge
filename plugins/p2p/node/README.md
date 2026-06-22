# P2P Node Plugin

`fcl::plugins::p2p::node` owns one shared `fcl_p2p` node and exposes typed
contribution APIs for protocol handlers and API-over-P2P publication.

## Identity

- Target: `fcl_plugins_p2p_node`
- Package component: `plugins_p2p_node`
- Plugin id: `fcl.plugins.p2p.node`
- Main API id: `fcl.plugins.p2p.node`
- Extra API ids:
  - `fcl.plugins.p2p.node.diagnostics_source`
  - `fcl.plugins.p2p.node.pubsub_source`
- Config section: `plugins.p2p.node`
- Public modules:
  - `fcl.plugins.p2p.node.plugin`
  - `fcl.plugins.p2p.node.api`
  - `fcl.plugins.p2p.node.types`
  - `fcl.plugins.p2p.node.exceptions`

## What It Provides

- Starts and stops a shared P2P node through the `fcl_app` lifecycle.
- Maps config into listen/bootstrap/advertised endpoints and relay/path policy.
- Lets application plugins publish typed APIs over a P2P protocol id.
- Opens typed remote API handles to peers through `remote<Interface>()`.
- Provides internal source APIs used by focused diagnostics and pubsub plugins.

The plugin does not implement product routing policy or durable application
queues. Core libp2p-style mechanics belong to `fcl_p2p`; this plugin composes
that substrate for applications.

## Config

```yaml
plugins:
   p2p:
      node:
         listen: ["/ip4/0.0.0.0/udp/9443/quic-v1"]
         bootstrap: ["/dns4/bootstrap.example/udp/9443/quic-v1"]
         advertised-endpoints: ["/dns4/node.example/udp/9443/quic-v1"]
         peer-id: ""
         certificate-pem: ""
         private-key-pem: ""
         api-codec: fcl.raw
         api:
            deadline-ms: 5000
            max-frame-size: 16777216
         max-inflight-per-peer: 64
         max-sessions: 1024
         max-protocol-handlers: 1024
         allow-insecure-test-mode: false
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

`allow-insecure-test-mode` is for local tests only. Deployed applications should
provide real identity material or an explicitly configured peer identity.

## Example

```cpp
import fcl.api.binding;
import fcl.plugins.p2p.node.api;
import fcl.plugins.p2p.node.plugin;

class catalog_p2p_plugin final : public fcl::app::plugin {
 public:
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto p2p = context.apis().get<fcl::plugins::p2p::node::api>(
         {.id = {"fcl.plugins.p2p.node"}, .major = 1});

      auto plan = fcl::api::binding()
         .serve(context.apis())
         .export_api<catalog_api>()
         .build();

      p2p->publish_api(std::move(plan), fcl::p2p::protocol_id{.value = "/catalog/api/1"});
      co_return;
   }
};
```

```cpp
registry.register_plugin(fcl::plugins::p2p::node::descriptor());
```
