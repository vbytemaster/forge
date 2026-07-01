# Forge Object Database Problem Notes

This note records the current problems found in the downstream `blockchain`
object database prototype and in Storlane mountd local-write plugins. The goal
is to prepare a neutral Forge object database library without moving
blockchain, FUSE, FSKit, Spring, content-storage or product policy semantics
into Forge.

## Why Forge Needs This Layer

Forge already has two database-related layers:

- `forge_rocksdb`: a concrete blocking RocksDB TransactionDB wrapper.
- `forge::plugins::db::rocksdb`: an app/plugin lifecycle API over that wrapper.

Those layers expose ordered key/value mechanics, but consumers still have to
hand-roll object ids, keyspaces, index records, cursor pages and mutation
records. Both donor areas show that this code is being repeated in product
layers.

The missing Forge layer starts smaller: a neutral `forge::objectdb` primitives
library for typed objects, stable ordered keys, index descriptors, cursor
boundaries and storage-neutral records/mutations. It is not a backend, runtime,
plugin, repository, session or transaction abstraction.

The donor baseline is tracked separately in
[`docs/donors/forge-objectdb-donor-baseline-v1.md`](../donors/forge-objectdb-donor-baseline-v1.md).
That note records which serious database systems influence each objectdb
component class.

The first prototype implementation has been quarantined under
`legacy/objectdb/`. It is archival reference material only: it is not built,
installed, exported as a package component or tested. New work should use this
document and the donor baseline instead of treating the quarantined prototype as
the public API shape.

## Active First Slice

The active first slice is now `forge_objectdb` with declarative object/index
descriptors, deterministic key layout and async store/index access:

- user object types derive from `object<Derived, Space, Type>`;
- the base owns `id` as `forge::ids::typed_id<Space, Type>` and contains no
  storage behavior;
- `object_index<T, indexed_by<...>>` is the schema descriptor;
- `primary_unique<Tag>` is shorthand for the base `id`;
- `FORGE_OBJECTDB_OBJECT(Object)` creates the inverse compile-time mapping from
  typed id to object descriptor;
- `secondary_unique` and `secondary_non_unique` describe stored index entries;
- `composite_key<&T::field1, &T::field2>` establishes lexicographic member
  order and partial prefix lookup through `std::make_tuple(...)`;
- `layout<Object>` produces byte-stable low-level record keys for tests and
  store/session internals;
- `cursor` and `page_request` are key-boundary primitives, not offset-based
  query state;
- `store.register_object<T>()`, `session`, `index_view`, `page`, `stream` and
  `for_each` execute over a caller-provided async storage context.

This slice intentionally has no catalog, runtime migration layer, plugin API,
app lifecycle, scheduler ownership, backend opening policy or concurrency
policy.

## Donor: `blockchain/libraries/db`

The `blockchain::db` prototype is the closest donor for the desired API shape.
It already contains:

- `object<T, Table>` and stable `object_id`;
- table and index identifiers;
- compile-time primary and secondary index descriptors;
- ordered key encoding;
- primary and secondary index maintenance;
- sessions with overlayed writes;
- changesets and a revision journal;
- a RocksDB adapter.

It should not be copied into Forge unchanged.

### Problems To Fix Before Porting

- The RocksDB adapter is built over the runtime plugin API
  `forge.plugins.db.rocksdb.api`. `forge::objectdb` must not depend on either
  the plugin API or `forge::rocksdb`; storage application belongs above this
  primitive layer.
- `session::scan_prefix(...)` pulls the full backend prefix and merges the
  session overlay in memory. This is not acceptable as the production scan
  primitive for large tables.
- Several secondary-index operations use broad scans over the entire index
  prefix before finding `lower_bound`, `upper_bound`, `next` or `previous`.
  Forge needs cursor-first and range-first operations from the start.
- Cursor behavior is prototype-grade. The cursor must be an opaque key boundary
  with tested first-page, next-page and end-of-stream behavior.
- Concurrency is currently one active writer session plus many readers. That is
  safe for chain-like execution, but it is a policy decision, not a universal
  object database truth.
- Ordered key encoding currently covers only the subset needed by the prototype.
  Forge needs a clearly owned key codec for unsigned/signed integers, strings,
  bytes, enums, object ids and composite keys.
- Revision changesets are useful, but they are not v1 primitives. Not every
  Forge object database consumer needs a chain-style revision journal.

### What Must Stay Outside Forge

- Spring compatibility and action/row semantics.
- Blockchain execution, producer, authorization or resource policy.
- Concrete chain object models.
- Any rule that assumes a Spring-compatible fork, transaction layout or account
  model.

## Donor: Storlane mountd Local Write Path

The mountd path is layered correctly for platform boundaries:

- `platform/macos_fskit` is a platform callback adapter.
- `files/vfs` owns POSIX-like local filesystem operations.
- `files/index`, `content/block`, `content/manifest`, `node/journal` and other
  plugins own durable domain records.
- RocksDB is consumed through `forge.plugins.db.rocksdb.api`.

The platform adapter does not write RocksDB directly and does not know storage,
Spring or content details. That boundary should remain.

### Problems Exposed By mountd Plugins

- Many plugins repeat the same database mechanics: `key_for_*` functions,
  record packing, prefix scans, cursor encoding, metrics records and
  transactional writer helpers.
- Cross-domain mutations are manually composed by passing one RocksDB
  transaction through several plugin-owned writers. This works, but it makes
  the transaction contract implicit and plugin-specific.
- Local writes rely on many small domain keyspaces, but there is no shared
  object/index vocabulary. Each plugin invents its own table and index layout.
- Cursor pagination exists in many places, but the behavior is owned locally by
  each plugin instead of by a common database layer.
- Error translation and conflict semantics are inconsistent across plugins
  because each plugin talks directly to the RocksDB plugin API.

### Parallel Write Problem

Mountd does allow multiple write operations to start concurrently. There is no
single global VFS writer mutex. Each mutation opens a RocksDB transaction.

Actual parallelism is weaker than it looks:

- Namespace operations lock parent namespace keys and concrete directory entry
  keys through RocksDB transaction locks.
- Journal and metadata writers lock counters and metrics keys.
- Content writers also lock shared metrics/counter keys.
- Those hot keys can serialize otherwise unrelated writes.
- Some domains use explicit locks, while others rely on TransactionDB conflict
  behavior and local validation.
- There is no single high-level retry, backoff or typed conflict policy for VFS
  write conflicts.

The current behavior is therefore a mixture of fine-grained locks, coarse hot
key serialization and backend conflict behavior. Forge should not turn that
accidental mix into the object database contract.

### What Must Stay Outside Forge

- FUSE and FSKit callback semantics.
- POSIX inode, directory, xattr and open-handle behavior.
- Dirty metadata/content conflict policy.
- Content heads, dirty extents, remote content fetches and manifests.
- Spring workspace/inode/content ids.
- Journal operation vocabulary such as `write_extent`, `rename`, `set_xattr`
  or sync checkpoints.

## Forge Direction

The library is top-level:

- namespace: `forge::objectdb`;
- target/component: `forge_objectdb` / `objectdb`;
- module prefix: `forge.objectdb.*`.

It sits below storage backends, app plugins and products. It provides stable
object/index components those layers can reuse; it does not open or own any
storage engine.

The reusable engine type is `forge::objectdb::store`, not
`forge::objectdb::database`. The word "database" describes the architecture
class, while `store` makes the C++ boundary clearer: it is an object/index
engine over an explicit storage context, not a runtime owner that opens files,
owns RocksDB, runs schedulers or exposes health/metrics.

The active scope includes:

- object identity through `forge::ids::object_id` and
  `forge::ids::typed_id<Space, Type>`;
- type descriptors;
- primary and secondary index descriptors;
- stable ordered key encoding;
- object and index key prefixes;
- prefix/range boundaries;
- opaque cursor/page request primitives;
- storage-neutral record execution;
- typed primitive validation errors.

Explicitly out of this block:

- catalog and migration runtime;
- backend contracts, ports or plugin APIs;
- repository APIs;
- concurrency, lock, retry or conflict policy;
- blockchain/FUSE/Spring/content semantics.

`forge::objectdb` must not introduce a parallel object-id model. The canonical
ID foundation is `forge::ids`, ported from the Storlane/BitShares-style
`{space,type,instance}` model.

Future layers can add migrations, snapshots, revision journals and richer
storage adapters, but those algorithms should still avoid product semantics and
should not make `forge_objectdb` own app/plugin lifecycle.

## Query And Index Execution Direction

The objectdb store should feel closer to Boost.MultiIndex than to a manual
RocksDB key helper layer:

```cpp
auto session = co_await store.session();

auto alice = co_await session.index<account_object, by_name>().find("alice");

auto page = co_await session.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .page({.limit = 100});
```

The important part is not the exact spelling yet, but the contract:

- `find`, `lower_bound`, `upper_bound` and `equal_range` operate only on
  declared indexes;
- composite indexes support full and partial prefix queries through
  `std::make_tuple(...)`;
- pagination is cursor/key-boundary based;
- large ranges can be consumed lazily with `stream().next()` or `for_each(...)`;
- non-indexed predicates must not be disguised as indexed lookup APIs.

RocksDB does not provide native objectdb secondary indexes. Forge must maintain
secondary index records itself:

- primary record: `object-id -> serialized object`;
- unique secondary index: `encoded index value -> object id`;
- non-unique secondary index:
  `encoded index value + primary object id -> marker/object id`.

RocksDB execution should use `Get`, iterator `Seek` and bounded range scans over
those records. It must not load the whole index into memory to emulate
`equal_range`. An in-memory backend can execute the same layout through an
ordered map and `lower_bound`; the public semantics stay the same.

## Migration Groundwork

Catalog and runtime migration support are intentionally not part of this block.
The current store uses `register_object<T>()` directly, similar to donor
`add_index<T>()` patterns, and keeps descriptor/key-layout determinism tested.

These are migration events and must become explicit in a future migration
block:

1. changing `{space, type}`;
2. changing index order or index kind;
3. changing a mapper/extractor or composite-key member order;
4. changing base object serialization;
5. changing the ordered key codec.

## Acceptance For The Future Implementation

- `forge_objectdb` does not import app/plugin APIs, `forge::rocksdb` or product
  libraries.
- The API exposes object descriptors, async store/session/index access and
  primitive validation helpers.
- Key prefixes and cursor boundaries are byte-stable and covered by golden
  tests.
- Composite indexes are byte-stable and covered by golden tests.
- In-memory and RocksDB storage contexts pass the same object/index behavior
  suite.
- Downstream Storlane plugins can remove local `key_for_*` and cursor helpers
  without moving Storlane product semantics into Forge.
