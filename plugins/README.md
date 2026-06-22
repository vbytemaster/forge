# FCL Plugins

Official FCL plugins are reusable infrastructure blocks for `fcl_app`
applications. A plugin owns lifecycle, configuration and shared resources, then
publishes narrow typed APIs that application plugins can use without reaching
into low-level transports directly.

Plugins are useful when an application needs one shared service, such as an
HTTP server, a P2P node, a signing provider or a secret provider, and many
application plugins need to contribute behavior to it.

## How Applications Use Plugins

1. Register official plugin descriptors in the application plugin registry.
2. Let `fcl::app::application_shell` collect schema-driven config for the app
   and registered plugins.
3. During `initialize()`, application plugins acquire local plugin APIs from
   `plugin_context::apis()`.
4. During startup, infrastructure plugins close contribution windows, start
   their owned resources and expose the composed runtime behavior.

```cpp
registry.register_plugin(fcl::plugins::http::server::descriptor());
registry.register_plugin(fcl::plugins::p2p::node::descriptor());
registry.register_plugin(fcl::plugins::signing::provider::descriptor());
registry.register_plugin(fcl::plugins::secret::provider::descriptor());
```

```cpp
auto http = context.apis().get<fcl::plugins::http::server::api>(
   {.id = {"fcl.plugins.http.server"}, .major = 1});

co_await http->publish<object_api>();
```

## Available Plugins

| Plugin | Target | Config section | Purpose |
| --- | --- | --- | --- |
| [`fcl::plugins::http::server`](http/server/README.md) | `fcl_plugins_http_server` | `plugins.http.server` | Runs one HTTP server and accepts typed HTTP API and middleware contributions. |
| [`fcl::plugins::p2p::node`](p2p/node/README.md) | `fcl_plugins_p2p_node` | `plugins.p2p.node` | Runs one P2P node and lets plugins publish protocols and typed remote APIs. |
| [`fcl::plugins::p2p::resolver`](p2p/resolver/README.md) | `fcl_plugins_p2p_resolver` | `plugins.p2p.resolver` | Publishes and resolves peer API metadata over the P2P node. |
| [`fcl::plugins::p2p::diagnostics`](p2p/diagnostics/README.md) | `fcl_plugins_p2p_diagnostics` | `plugins.p2p.diagnostics` | Exposes read-only P2P network/resource/pubsub diagnostics. |
| [`fcl::plugins::p2p::pubsub`](p2p/pubsub/README.md) | `fcl_plugins_p2p_pubsub` | `plugins.p2p.pubsub` | Exposes topic publish/subscribe over the shared P2P node. |
| [`fcl::plugins::signing::provider`](signing/provider/README.md) | `fcl_plugins_signing_provider` | `plugins.signing.provider` | Signs digests with configured local keys and output profiles. |
| [`fcl::plugins::secret::provider`](secret/provider/README.md) | `fcl_plugins_secret_provider` | `plugins.secret.provider` | Provides bounded secret retrieval, derivation and symmetric encryption operations. |

The aggregate target `fcl_plugins` and package component `plugins` are
convenience dependencies. Prefer focused targets/components in small consumers:

```cmake
find_package(FCL REQUIRED COMPONENTS plugins_http_server)
target_link_libraries(app PRIVATE FCL::fcl_plugins_http_server)
```

## Public Module Shape

Each official plugin follows the same public module layout:

- `fcl.plugins.<family>.<name>.plugin` provides `plugin` and `descriptor()`.
- `fcl.plugins.<family>.<name>.api` provides typed contracts exposed through
  `fcl_api`.
- `fcl.plugins.<family>.<name>.types` provides config and DTO types.
- `fcl.plugins.<family>.<name>.exceptions` provides typed exceptions.

Plugins that need an extra public slice, such as HTTP middleware, keep that
slice under the same leaf namespace. Grouping namespaces like
`fcl::plugins::p2p`, `fcl::plugins::http`, `fcl::plugins::signing` and
`fcl::plugins::secret` are empty.

## Boundaries

Official plugins are not mini-applications and do not expose raw lifecycle or
transport mutation APIs. Their public APIs are typed contribution surfaces:

- HTTP APIs are published through `publish<Interface>()`, not raw route verbs.
- P2P APIs are published through typed `fcl::api::binding_plan` values.
- Config is decoded through `BOOST_DESCRIBE_STRUCT`, `fcl_schema` rules and
  `fcl_config`.
- Product policy, auth, billing, storage semantics and downstream vocabulary do
  not belong in FCL plugins.
