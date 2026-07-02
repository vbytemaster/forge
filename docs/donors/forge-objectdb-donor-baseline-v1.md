# Forge Object Database Donor Baseline

This note records the serious donor systems and database classes that should
shape a future `forge::objectdb` library. The goal is not to copy one object
database, but to combine proven patterns for identifiers, ordered keys, indexes,
transactions, snapshots, cursors, concurrency and recovery.

`forge::objectdb` is not implemented by this note. It must remain a neutral
library of primitives and algorithms. It must not become a RocksDB backend,
runtime plugin, blockchain database, FUSE database or product policy layer.

## Current Foundation

Forge now owns `forge::ids` as the canonical ID foundation:

- `forge::ids::object_id { space, type, instance }`;
- `forge::ids::typed_id<Space, Type>`;
- raw serialization and variant conversion compatible with the
  Storlane/BitShares-style identity model.

A future `forge::objectdb` must reuse `forge::ids`. It must not introduce a
parallel fake object identifier.

## Database Classes

Different systems solve different problems. Before adding a component, decide
which database class it belongs to.

### Ordered Key/Value Store

An ordered key/value store owns byte keys, byte values, prefix scans and
range scans. It does not know objects or indexes.

Examples: RocksDB, Pebble, FoundationDB storage layer.

Forge status:

- `forge::rocksdb` already wraps one concrete ordered backend.
- `forge::objectdb` must not expose backend methods such as `get`, `put`,
  `erase`, `begin`, `commit` or `rollback` in its first primitive layer.

### Object Store

An object store owns stable object identity, object type metadata and typed
object records.

Examples: BitShares/Graphene object model, Storlane `ids`.

Forge direction:

- object identity comes from `forge::ids`;
- object metadata and type descriptors may live in `forge::objectdb`;
- object storage remains storage-neutral.

### Indexed Object Database

An indexed object database owns primary indexes, secondary indexes, unique and
non-unique indexes, composite keys, index descriptors and cursor boundaries.

Examples: Graphene `multi_index`, MongoDB/WiredTiger secondary index ideas,
PostgreSQL index/catalog discipline.

Forge direction:

- index descriptors should be typed and deterministic;
- composite index keys should use a Forge-owned ordered key codec;
- local `key_for_*` helpers in downstream products should eventually disappear.

### Transactional Object Database

A transactional object database owns atomic multi-object mutation planning,
commit/rollback boundaries, conflict reporting and validation hooks.

Examples: PostgreSQL transactions, SQLite transactions, RocksDB TransactionDB,
chainbase undo sessions.

Forge direction:

- first `objectdb` slice should not own runtime transactions;
- it may later provide pure mutation planning, for example
  `insert/update/erase -> mutation_batch`;
- the backend or application layer applies mutations atomically.

### MVCC Object Database

MVCC means multi-version concurrency control: readers see a stable snapshot
while writers create newer versions. MVCC is the main donor pattern for
parallel read/write behavior.

Examples: PostgreSQL, LMDB, CockroachDB, FoundationDB read versions.

Forge direction:

- snapshots and read visibility should be explicit concepts if added;
- concurrency policy should not be hidden behind accidental backend behavior;
- readers, writers, retries and conflicts need typed contracts before they
  become public API.

### Domain State Database

A domain state database adds product or protocol rules on top of objects:
chain execution, FUSE namespace rules, content manifests, account models,
authorization, billing or fork/replay policy.

Examples: EOSIO controller state, blockchain state databases, Storlane mountd
local branch state.

Forge direction:

- these semantics stay outside `forge::objectdb`;
- `forge::objectdb` may supply reusable components, but products own their
  invariants.

### Query Database

A query database owns a planner, query language, optimizer and broad execution
model.

Examples: PostgreSQL SQL planner, MongoDB query planner, CockroachDB SQL layer.

Forge direction:

- this is out of scope;
- Forge may later provide cursor and range primitives, not a SQL-like planner.

## Donor Systems

### Storlane IDs And BitShares-Style IDs

Accepted:

- identity shape `{space, type, instance}`;
- typed wrappers such as `typed_id<Space, Type>`;
- stable string form `space/type/instance`;
- raw and variant conversion behavior.

Rejected:

- product-specific Storlane IDs or downstream object spaces inside Forge;
- a second object ID model inside `forge::objectdb`.

### Graphene / BitShares / Chainbase

Accepted:

- object identity plus object type separation;
- typed indexes and deterministic index registration;
- primary and secondary indexes over objects;
- undo/session thinking for chain-like deterministic state transitions.

Use with care:

- chainbase is strong for deterministic single-chain execution;
- it is weaker as a general modern concurrent database model;
- do not inherit single-writer assumptions as universal Forge policy.

Rejected:

- chain-specific execution, evaluator, fork or authority semantics;
- treating wipe/replay upgrade mechanics as Forge's general migration model.

### EOSIO / Antelope

Accepted:

- clear separation between controller/runtime ownership and lower database
  mechanics;
- snapshot/replay/reversible-state boundaries as design references;
- explicit protocol-feature gates as a reminder that state schema evolution is
  a first-class problem.

Rejected:

- embedding controller, WASM, fork database, block log, resource limits or
  authorization policy into `forge::objectdb`;
- making Forge object database a blockchain runtime.

### PostgreSQL

Accepted:

- MVCC snapshots and explicit transaction isolation as the gold-standard donor
  for parallel readers and writers;
- typed lock modes and typed conflict behavior;
- catalog/schema discipline for indexes and object types;
- cursor and page thinking that does not require loading the full result set;
- WAL and recovery discipline as correctness donors.

Rejected:

- SQL planner, parser and optimizer scope;
- making objectdb a relational database.

### FoundationDB

Accepted:

- ordered keyspace composition;
- tuple-like key encoding for composite keys;
- directory-layer style namespacing;
- conflict ranges as a clean way to explain read/write conflict ownership;
- opaque cursors and range boundaries over ordered keys.

Rejected:

- distributed transaction runtime;
- networked cluster semantics;
- exposing backend-specific conflict ranges as the first public Forge API.

### RocksDB / Pebble

Accepted:

- ordered byte key/value mechanics;
- prefix/range scan behavior;
- column-family and keyspace isolation lessons;
- snapshots, iterators and write batches as backend donor mechanics;
- compaction-safe key layout discipline.

Important boundary:

- RocksDB and Pebble do not provide Forge objectdb secondary indexes as a
  native database feature. Forge must store secondary index entries as ordinary
  ordered key/value records, then execute indexed access with exact lookup,
  iterator seek, bounded range scans and opaque cursors.

Rejected:

- native `rocksdb::*` types in `forge::objectdb`;
- treating RocksDB TransactionDB as the objectdb API;
- leaking backend status or lifecycle into object/index primitives.

### SQLite

Accepted:

- simple, rigorous transaction boundaries;
- crash/atomicity expectations;
- WAL thinking as a correctness donor;
- deterministic local behavior as a useful test oracle.

Rejected:

- SQL language and planner scope;
- page-cache implementation details as public API.

### LMDB

Accepted:

- single-writer, multi-reader MVCC as a simple concurrency donor;
- cheap read transactions and stable snapshots;
- clear transaction lifetime rules.

Rejected:

- forcing one-writer semantics as the only Forge model;
- memory-mapped backend details in public objectdb primitives.

### MongoDB / WiredTiger

Accepted:

- document/object plus secondary-index ergonomics;
- cursor APIs over large result sets;
- update semantics as a warning against implicit full-object rewrites.

Rejected:

- document database query language scope;
- dynamic schema as a replacement for Forge typed descriptors.

### CockroachDB

Accepted:

- MVCC key encoding lessons;
- transaction conflict and retry discipline;
- range-oriented thinking for large ordered keyspaces;
- schema/index separation in a distributed setting.

Rejected:

- distributed SQL runtime;
- consensus, leaseholder or range-replication semantics.

## Forge Component Map

The desired direction is a layered set of components, not a monolithic
database.

### Already Present

- `forge::ids`: canonical object identity primitives.
- `forge::rocksdb`: concrete ordered key/value backend wrapper.
- `forge::plugins::db::rocksdb`: app/plugin lifecycle and management API over
  `forge::rocksdb`.

### Future `forge::objectdb` Primitive Candidates

- `object_traits` and object type metadata;
- object record wrappers over described value types;
- table/type/index descriptors;
- primary and secondary index descriptors;
- ordered key codec for unsigned/signed integers, strings, bytes, enums,
  `forge::ids::object_id` and composites;
- key prefixes and key ranges;
- opaque cursor tokens and page requests;
- storage-neutral records and mutation records;
- primitive validation errors.

### Future `forge::objectdb::store`

The reusable object database engine should be named `store`, not `database`.
The term "object database" remains the architecture class, but the C++ owner
type should communicate that it is an object/index storage layer over an
already provided storage context.

Preferred vocabulary:

- `forge::objectdb::store`: reusable object/index engine;
- `store.register_object<T>()`: direct object descriptor registration, matching
  donor `add_index<T>()` patterns;
- `forge::objectdb::session`: staged object mutations and index maintenance;
- `forge::objectdb::snapshot`: stable read visibility;
- `forge::objectdb::cursor` / `forge::objectdb::page`: paginated reads;
- `forge::rocksdb::store`: physical ordered key/value backend.

Reasoning:

- `database` sounds like it owns process/runtime/backend lifecycle: opening
  RocksDB, WAL, files, scheduler, metrics and health.
- `store` fits the Forge boundary better: it is a reusable component over an
  explicit storage/transaction context.
- Different products should define different object schemas, not different
  object database engines.
- `forge::objectdb::store` and `forge::rocksdb::store` are not ambiguous
  because their namespaces carry different layers: object/index engine versus
  physical ordered key/value backend.
- A catalog/migration layer is still expected later, but it should be designed
  as an explicit migration block rather than hidden inside the first async store
  slice.

### Future Index Views

The store/session layer should expose typed index views inspired by
Boost.MultiIndex:

- `session.index<Object, Tag>()` returns a typed view over a declared index tag;
- unique indexes support `find(key)`;
- unique and non-unique ordered indexes support `lower_bound`, `upper_bound`
  and `equal_range`;
- composite indexes support partial prefix lookup, for example
  `equal_range(std::make_tuple(region))` for an index declared as
  `(region, balance)`;
- large results are read through `page({limit, cursor})`, not materialized as a
  full vector.
- very large results can also be consumed lazily through `stream().next()` or
  `for_each(...)`.

This is not a SQL/query-planner layer. Query execution is still based on the
declared object indexes and the ordered key layout:

- primary object record: `object-id -> serialized object`;
- unique secondary index: `encoded index value -> object id`;
- non-unique secondary index: `encoded index value + primary id -> marker/id`;
- exact unique lookup: backend `get(index key)` then `get(object key)`;
- prefix/range lookup: backend `lower_bound` or iterator `seek`, then bounded
  reads until the key leaves the range.

Backends differ only in mechanics. A RocksDB implementation uses native
`Get`/iterator seek over persisted bytes. An in-memory implementation can use
ordered containers over the same record keys. Neither implementation should
load a whole index into memory to satisfy `find` or `equal_range`.

### Future Higher Layers

These may be useful later, but should not be part of the first primitive slice:

- mutation planning algorithms;
- automatic index maintenance;
- sessions or revision journals;
- MVCC snapshots;
- conflict/retry policy;
- repository APIs;
- backend adapters.

## Concurrency Direction

Concurrency must be explicit, not an accident of a backend.

Accepted baseline:

- readers and writers should have named visibility rules;
- cursors should not require full-prefix materialization;
- hot keys and counters must be visible design choices;
- conflicts should be typed, not parsed from backend text;
- retry/backoff policy belongs above primitive key/index components until the
  transaction model is intentionally designed.

Rejected baseline:

- one global mutex as the objectdb correctness mechanism;
- silent full-prefix scans under cursor APIs;
- hidden RocksDB TransactionDB conflicts as the public Forge behavior;
- assuming chain-style single-writer execution for all consumers.

## First Implementation Rule

The next implementation pass should still be primitives-first:

1. Use `forge::ids` for object identity.
2. Add ordered key and descriptor primitives.
3. Prove byte-stable keys with golden tests.
4. Add cursor/page primitives before any scan algorithm.
5. Keep backend, runtime, plugin and product semantics out of `forge::objectdb`.

If a proposed type needs a storage engine, scheduler, plugin lifecycle, FUSE
callback, blockchain replay rule or SQL planner to make sense, it does not
belong in the first `forge::objectdb` primitive layer.
