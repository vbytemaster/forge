# forge_objectdb

`forge_objectdb` is the neutral primitive layer for building object databases.
It does not open storage, own transactions, schedule work, or depend on
plugins. Runtime code remains responsible for applying the records and
mutations to RocksDB, memory stores, caches, or product-specific persistence.

## Public Modules

- `forge.objectdb.types` provides stable IDs, table/index descriptors and
  schema tags.
- `forge.objectdb.key` provides deterministic ordered key construction,
  object/index key prefixes and prefix ranges.
- `forge.objectdb.cursor` provides opaque cursor/page request primitives.
- `forge.objectdb.index` provides compile-time descriptor concepts.
- `forge.objectdb.record` provides record and mutation value types.
- `forge.objectdb.exceptions` provides typed primitive validation errors.

## Boundaries

Use `forge_objectdb` when a library or plugin needs shared object IDs,
keyspace discipline, index descriptors, cursors or mutation records.

Do not use this library as a backend abstraction. It intentionally has no
`store`, `session`, `transaction`, `repository`, `get`, `put`, `scan` or
`commit` API. Higher-level storage code may build on these primitives, but it
must own its own runtime and persistence boundary.

## Donor Notes

The primitive shape is informed by object-database donors such as Graphene and
chainbase, plus RocksDB ordered-key constraints. The rejected donor mistakes are
equally important: no plugin API dependency, no product object vocabulary, no
full-prefix pagination requirement, and no accidental concurrency policy baked
into this layer.
