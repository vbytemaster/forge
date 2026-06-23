# P2P Pubsub Plugin

`forge::plugins::p2p::pubsub` exposes a typed application facade for topic
publish/subscribe over the shared P2P node.

## Identity

- Target: `forge_plugins_p2p_pubsub`
- Package component: `plugins_p2p_pubsub`
- Plugin id: `forge.plugins.p2p.pubsub`
- Main API id: `forge.plugins.p2p.pubsub`
- Config section: `plugins.p2p.pubsub`
- Depends on plugin id: `forge.plugins.p2p.node`
- Public modules:
  - `forge.plugins.p2p.pubsub.plugin`
  - `forge.plugins.p2p.pubsub.api`
  - `forge.plugins.p2p.pubsub.types`
  - `forge.plugins.p2p.pubsub.exceptions`

## What It Provides

- Publish raw byte messages to a topic.
- Publish described/serializable values through the typed helper overload.
- Subscribe with async validation handlers.
- Track subscriptions and expose bounded pubsub snapshots.

It does not provide durable queues, replay, business-level fan-out or product
delivery guarantees. Those policies belong above this plugin.

## Config

```yaml
plugins:
   p2p:
      pubsub:
         max-topics: 1024
         max-handlers-per-topic: 64
         max-active-handlers: 4096
         max-message-size: 1048576
         handler-deadline-ms: 5000
         allowed-topics: []
         denied-topics: []
         sign-publishes: true
```

## Example

```cpp
import forge.plugins.p2p.pubsub.api;
import forge.plugins.p2p.pubsub.plugin;

auto pubsub = context.apis().get<forge::plugins::p2p::pubsub::api>(
   {.id = {"forge.plugins.p2p.pubsub"}, .major = 1});

auto subscription = co_await pubsub->subscribe(
   forge::p2p::pubsub::topic{.value = "catalog.updates"},
   [](forge::plugins::p2p::pubsub::message value)
      -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
      consume(value.data);
      co_return forge::p2p::pubsub::validation_result::accept;
   });

co_await pubsub->publish(
   forge::p2p::pubsub::topic{.value = "catalog.updates"},
   std::vector<std::uint8_t>{1, 2, 3});
```

```cpp
registry.register_plugin(forge::plugins::p2p::node::descriptor());
registry.register_plugin(forge::plugins::p2p::pubsub::descriptor());
```
