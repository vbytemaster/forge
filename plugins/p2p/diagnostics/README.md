# P2P Diagnostics Plugin

`forge::plugins::p2p::diagnostics` exposes read-only diagnostics for the shared
P2P node. It is intended for operators, tests and application plugins that need
bounded snapshots of network state without depending on private node internals.

## Identity

- Target: `forge_plugins_p2p_diagnostics`
- Package component: `plugins_p2p_diagnostics`
- Plugin id: `forge.plugins.p2p.diagnostics`
- Main API id: `forge.plugins.p2p.diagnostics`
- Config section: `plugins.p2p.diagnostics`
- Depends on plugin id: `forge.plugins.p2p.node`
- Public modules:
  - `forge.plugins.p2p.diagnostics.plugin`
  - `forge.plugins.p2p.diagnostics.api`
  - `forge.plugins.p2p.diagnostics.types`
  - `forge.plugins.p2p.diagnostics.exceptions`

## What It Provides

- Network snapshot and network-state reads.
- Resource-manager snapshot reads.
- Pubsub snapshot reads when pubsub is enabled.
- Peer listing and single-peer lookup with bounded limits.

It is read-only. It does not add HTTP endpoints, logging sinks or product health
semantics by itself.

## Config

```yaml
plugins:
   p2p:
      diagnostics:
         max-peers: 1024
         max-sessions: 1024
         max-endpoints-per-peer: 64
         max-protocols-per-peer: 128
         max-relay-reservations-per-peer: 64
```

## Example

```cpp
import forge.plugins.p2p.diagnostics.api;
import forge.plugins.p2p.diagnostics.plugin;

auto diagnostics = context.apis().get<forge::plugins::p2p::diagnostics::api>(
   {.id = {"forge.plugins.p2p.diagnostics"}, .major = 1});

auto network = diagnostics->network();
auto resources = diagnostics->resources();
auto peers = diagnostics->peers({.only_connected = true, .limit = 100});
```

```cpp
registry.register_plugin(forge::plugins::p2p::node::descriptor());
registry.register_plugin(forge::plugins::p2p::diagnostics::descriptor());
```
