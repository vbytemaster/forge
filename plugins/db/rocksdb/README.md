# RocksDB Plugin

`forge::plugins::db::rocksdb` owns a local RocksDB TransactionDB boundary for
Forge applications. It exposes a local typed API for point reads, writes,
transactions, bounded prefix scans and WAL flushes.

It is the only official plugin layer that should touch RocksDB directly.

## When To Use

- An application needs one configured local RocksDB TransactionDB service.
- Multiple plugins should share a database API without linking native RocksDB
  types.
- Plugin code needs point reads, writes, transactions, WAL flushes and bounded
  prefix scans through Forge APIs.

## When Not To Use

- Do not use this plugin from low-level libraries that need direct store
  ownership. Use `forge_rocksdb`.
- Do not use it as a product schema, replication or retention layer.
- Do not perform unbounded prefix scans through the API.

## Target And Modules

- Target: `forge_plugins_db_rocksdb`
- Package component: `plugins_db_rocksdb`
- Plugin id/API id: `forge.plugins.db.rocksdb`
- Config section: `plugins.db.rocksdb`
- Public modules: `forge.plugins.db.rocksdb.{plugin,api,types,exceptions}`

## Dependencies

- `forge_app`
- `forge_api`
- `forge_rocksdb`
- `forge_config`
- `forge_schema`

## Config

```yaml
plugins:
  db:
    rocksdb:
      path: "./data/rocksdb"
      column-families:
        - "meta"
        - "data"
      create-if-missing: true
      create-missing-column-families: true
```

## Examples

Application plugins acquire the local API through `plugin_context::apis()` and
use transactions for atomic multi-key updates. `flush_wal(sync=true)` is the
local durability boundary exposed by the plugin.

```cpp
import forge.plugins.db.rocksdb.api;
import forge.plugins.db.rocksdb.plugin;

registry.register_plugin(forge::plugins::db::rocksdb::descriptor());

auto db = context.apis().get<forge::plugins::db::rocksdb::api>(
   {.id = {"forge.plugins.db.rocksdb"}, .major = 1});

auto page = co_await db->scan_page(
   forge::rocksdb::family{"meta"},
   forge::rocksdb::scan_request{
      .prefix = forge::rocksdb::to_bytes("item:"),
      .limit = 100,
   });
```

## Boundaries

- RocksDB native types do not escape public Forge modules.
- The plugin does not define filesystem, content, journal or product storage
  semantics.
- In-memory, SQLite or remote database backends are separate future plugins, not
  hidden modes of this plugin.

## Security And Common Mistakes

- Database path is operator-controlled local config; never derive it from
  remote input.
- Keep product keyspace layout in consuming plugins.
- Do not hold transactions across plugin shutdown.
- Use paged scans with limits rather than materializing full prefixes.

## Tests

Covered by `test_forge_plugins_db_rocksdb` and package component tests when
Forge is built with RocksDB support.
