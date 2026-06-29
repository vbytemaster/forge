# forge_rocksdb

`forge_rocksdb` is the reusable Forge wrapper around RocksDB `TransactionDB`.
Use it when Forge code needs a local durable key/value backend with column
families, ordered prefix scans, cursor pages and explicit transaction
boundaries.

This library is the owner of native RocksDB status mapping. Public modules do
not expose `::rocksdb::*`; native handles, options and iterators stay private.

## When To Use

- A library needs direct blocking RocksDB access without app/plugin lifecycle.
- A component needs TransactionDB commit/rollback semantics.
- Keys are byte-oriented and need stable ordered prefix scans.
- A plugin wants to expose database operations while keeping RocksDB mechanics
below the plugin boundary.

Use `forge::plugins::db::rocksdb` instead when the database should be configured
from an application config section and exposed as a local Forge API.

## When Not To Use

- Do not use `forge_rocksdb` as a generic database abstraction. It is the
  concrete RocksDB backend.
- Do not call it directly from application plugins that should depend on a
  configured database service. Use `forge::plugins::db::rocksdb`.
- Do not put product schema, replication, authorization or retention policy in
  this library.
- Do not use it for async scheduling. Offload blocking work in the caller or in
  the plugin layer.

## Public Modules

- `forge.rocksdb.types` - config, column-family, key/value, operation and scan
  DTOs.
- `forge.rocksdb.exceptions` - typed `forge.rocksdb` exception category.
- `forge.rocksdb.store` - blocking `store` and move-only `transaction`.

Target: `forge_rocksdb`.

Component: `rocksdb`.

Dependencies: `forge_exceptions`, `forge_schema` and private
`RocksDB::rocksdb`. The library is built only when Forge is configured with
RocksDB support.

## Examples

### Open A Store And Write Values

```cpp
import forge.rocksdb.store;

auto db = forge::rocksdb::store{
   forge::rocksdb::config{
      .path = "./node.db",
      .column_families = {"meta", "peers"},
   }};

auto meta = forge::rocksdb::family{"meta"};
db.put(
   meta,
   forge::rocksdb::make_key("schema-version"),
   forge::rocksdb::to_bytes("1"),
   forge::rocksdb::write_options{.sync = true});
```

### Use A Transaction

```cpp
import forge.rocksdb.store;

auto tx = db.begin(forge::rocksdb::write_options{.sync = true});
tx.put(
   forge::rocksdb::family{"peers"},
   forge::rocksdb::make_key("peer:12D3..."),
   forge::rocksdb::to_bytes("connected"));
tx.commit();
```

If a `transaction` is destroyed or move-assigned while still active, it performs
a best-effort rollback. `commit()` and `rollback()` close the transaction.

### Page Through A Prefix

```cpp
import forge.rocksdb.store;

auto first = db.scan_page(
   forge::rocksdb::family{"peers"},
   forge::rocksdb::scan_request{
      .prefix = forge::rocksdb::to_bytes("peer:"),
      .limit = 100,
   });

if (!first.next_cursor.empty()) {
   auto next = db.scan_page(
      forge::rocksdb::family{"peers"},
      forge::rocksdb::scan_request{
         .prefix = forge::rocksdb::to_bytes("peer:"),
         .cursor = first.next_cursor,
         .limit = 100,
      });
}
```

The cursor is an opaque key boundary. Callers should store and pass it back
unchanged.

## Tests

- `test_forge_rocksdb` covers open/config validation, persistence, prefix scans,
  cursor pages, batch writes, transaction commit/rollback, move assignment,
  lock-only paths and typed error mapping.
- `test_forge_package_rocksdb_component` verifies
  `find_package(Forge CONFIG REQUIRED COMPONENTS rocksdb)` and
  `Forge::forge_rocksdb`.
- `test_forge_plugins_db_rocksdb` and `test_forge_quic_p2p` cover plugin and P2P
  consumers over this backend.

## Security And Durability Notes

- Native RocksDB status and filesystem failures are mapped to typed
  `forge.rocksdb` exceptions before crossing the public boundary.
- Transactions must be committed, rolled back or allowed to perform best-effort
  rollback on destruction. Do not keep transaction objects alive past their
  owner store/plugin lifetime.
- Prefix scans must use `scan_page(...)` with a caller-owned limit. Avoid
  materializing an unbounded prefix into memory.
- Paths are operator-controlled local configuration. Do not accept remote input
  as a database path.

## Common Mistakes

- Treating cursors as user-facing tokens. They are opaque key boundaries for the
  same store and prefix.
- Mixing product key semantics into this wrapper. Keep key layout ownership in
  the consuming library or plugin.
- Catching `std::filesystem::filesystem_error` or native RocksDB status at
  higher layers. The public boundary should see typed Forge exceptions.
- Assuming `write_options{.sync = true}` replaces product-level checkpoints or
  journal semantics. It only controls RocksDB write durability.

## Boundaries

- `forge_rocksdb` is a concrete RocksDB backend, not a generic database
  abstraction.
- Native RocksDB types, handles, iterators and status objects are private.
- Filesystem and RocksDB failures are mapped to typed `forge.rocksdb`
  exceptions before crossing the public boundary.
- App config, scheduler offload and plugin lifecycle belong to
  `forge::plugins::db::rocksdb`, not this library.
- Product storage policy, replication, auth and data placement are outside this
  library.
