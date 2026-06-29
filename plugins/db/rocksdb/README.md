# RocksDB Plugin

`forge::plugins::db::rocksdb` owns a local RocksDB TransactionDB boundary for
Forge applications. It exposes a local typed API for point reads, writes,
transactions, bounded prefix scans and WAL flushes.

It is the only official plugin layer that should touch RocksDB directly.

## Target And Modules

- Target: `forge_plugins_db_rocksdb`
- Package component: `plugins_db_rocksdb`
- Plugin id/API id: `forge.plugins.db.rocksdb`
- Config section: `plugins.db.rocksdb`
- Public modules: `forge.plugins.db.rocksdb.{plugin,api,types,exceptions}`

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

## Usage

Application plugins acquire the local API through `plugin_context::apis()` and
use transactions for atomic multi-key updates. `flush_wal(sync=true)` is the
local durability boundary exposed by the plugin.

## Boundaries

- RocksDB native types do not escape public Forge modules.
- The plugin does not define filesystem, content, journal or product storage
  semantics.
- In-memory, SQLite or remote database backends are separate future plugins, not
  hidden modes of this plugin.

## Tests

Covered by `test_forge_plugins_db_rocksdb` and package component tests when
Forge is built with RocksDB support.
