# FORGE Plugins

Official FORGE plugins are reusable infrastructure blocks for `forge_app`
applications. A plugin owns lifecycle, configuration and shared resources, then
publishes narrow typed APIs that application plugins can use without reaching
into low-level transports directly.

Plugins are useful when an application needs one shared service, such as an
HTTP server, a P2P node, a crypto signer or a crypto secrets service, and many
application plugins need to contribute behavior to it.

## How Applications Use Plugins

1. Register official plugin descriptors in the application plugin registry.
2. Let `forge::app::application_shell` collect schema-driven config for the app
   and registered plugins.
3. During `initialize()`, application plugins acquire local plugin APIs from
   `plugin_context::apis()`.
4. During startup, infrastructure plugins close contribution windows, start
   their owned resources and expose the composed runtime behavior.

```cpp
registry.register_plugin(forge::plugins::http::server::descriptor());
registry.register_plugin(forge::plugins::p2p::node::descriptor());
registry.register_plugin(forge::plugins::crypto::signer::descriptor());
registry.register_plugin(forge::plugins::crypto::secrets::descriptor());
registry.register_plugin(forge::plugins::log::otlp::descriptor());
registry.register_plugin(forge::plugins::db::rocksdb::descriptor());
```

```cpp
auto http = context.apis().get<forge::plugins::http::server::api>(
   {.id = {"forge.plugins.http.server"}, .major = 1});

co_await http->publish<object_api>();
```

## Available Plugins

| Plugin | Target | Config section | Purpose |
| --- | --- | --- | --- |
| [`forge::plugins::http::server`](http/server/README.md) | `forge_plugins_http_server` | `plugins.http.server` | Runs one HTTP server and accepts typed HTTP API and middleware contributions. |
| [`forge::plugins::p2p::node`](p2p/node/README.md) | `forge_plugins_p2p_node` | `plugins.p2p.node` | Runs one P2P node and lets plugins publish protocols and typed remote APIs. |
| [`forge::plugins::p2p::resolver`](p2p/resolver/README.md) | `forge_plugins_p2p_resolver` | `plugins.p2p.resolver` | Publishes and resolves peer API metadata over the P2P node. |
| [`forge::plugins::p2p::diagnostics`](p2p/diagnostics/README.md) | `forge_plugins_p2p_diagnostics` | `plugins.p2p.diagnostics` | Exposes read-only P2P network/resource/pubsub diagnostics. |
| [`forge::plugins::p2p::pubsub`](p2p/pubsub/README.md) | `forge_plugins_p2p_pubsub` | `plugins.p2p.pubsub` | Exposes topic publish/subscribe over the shared P2P node. |
| [`forge::plugins::crypto::signer`](crypto/signer/README.md) | `forge_plugins_crypto_signer` | `plugins.crypto.signer` | Signs digests with configured local keys and output profiles. |
| [`forge::plugins::crypto::secrets`](crypto/secrets/README.md) | `forge_plugins_crypto_secrets` | `plugins.crypto.secrets` | Provides bounded secret retrieval, derivation and symmetric encryption operations. |
| [`forge::plugins::log::otlp`](log/otlp/README.md) | `forge_plugins_log_otlp` | `plugins.log.otlp` | Exports configured FORGE logger routes to OTLP/HTTP JSON. |
| [`forge::plugins::db::rocksdb`](db/rocksdb/README.md) | `forge_plugins_db_rocksdb` | `plugins.db.rocksdb` | Provides a local RocksDB TransactionDB service for infrastructure plugins that need durable key/value state. |

The aggregate target `forge_plugins` and package component `plugins` are
convenience dependencies. Prefer focused targets/components in small consumers:

```cmake
find_package(Forge REQUIRED COMPONENTS plugins_http_server)
target_link_libraries(app PRIVATE Forge::forge_plugins_http_server)
```

## Public Module Shape

Each official plugin follows the same public module layout:

- `forge.plugins.<family>.<name>.plugin` provides `plugin` and `descriptor()`.
- `forge.plugins.<family>.<name>.api` provides typed contracts exposed through
  `forge_api`.
- `forge.plugins.<family>.<name>.types` provides config and DTO types.
- `forge.plugins.<family>.<name>.exceptions` provides typed exceptions.

Plugins that need an extra public slice, such as HTTP middleware, keep that
slice under the same leaf namespace. Grouping namespaces like
`forge::plugins::p2p`, `forge::plugins::http`, `forge::plugins::crypto` and
`forge::plugins::db` are empty.

## Boundaries

Official plugins are not mini-applications and do not expose raw lifecycle or
transport mutation APIs. Their public APIs are typed contribution surfaces:

- HTTP APIs are published through `publish<Interface>()`, not raw route verbs.
- P2P APIs are published through typed `forge::api::binding_plan` values.
- Config is decoded through `BOOST_DESCRIBE_STRUCT`, `forge_schema` rules and
  `forge_config`.
- Product policy, auth, billing, storage semantics and downstream vocabulary do
  not belong in FORGE plugins.
