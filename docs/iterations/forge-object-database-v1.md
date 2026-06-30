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

The likely library should be top-level:

- namespace: `forge::objectdb`;
- target/component: `forge_objectdb` / `objectdb`;
- module prefix: `forge.objectdb.*`.

It should sit below storage backends, app plugins and products. It provides
stable primitives those layers can reuse; it does not apply them to any
storage engine.

Initial primitives-only scope should include:

- typed object ids and table ids;
- primary and secondary index descriptors;
- stable ordered key encoding;
- object and index key prefixes;
- prefix/range boundaries;
- opaque cursor/page request primitives;
- storage-neutral records and mutation records;
- typed primitive validation errors.

Explicitly out of v1:

- backend contracts or ports;
- `get`, `put`, `erase`, `scan`, `begin`, `commit` or `rollback`;
- repositories, sessions or transaction managers;
- automatic index-maintenance algorithms;
- concurrency, lock, retry or conflict policy;
- blockchain/FUSE/Spring/content semantics.

The next layer can add pure planning algorithms, for example
`insert/update/erase -> mutation_batch` and `range_query -> key_range`, but those
algorithms should still avoid owning a backend/runtime.

## Acceptance For The Future Implementation

- `forge_objectdb` does not import app/plugin APIs, `forge::rocksdb` or product
  libraries.
- The v1 API exposes only primitives and primitive validation helpers.
- Key prefixes and cursor boundaries are byte-stable and covered by golden
  tests.
- Composite indexes are byte-stable and covered by golden tests.
- Downstream Storlane plugins can remove local `key_for_*` and cursor helpers
  without moving Storlane product semantics into Forge.
