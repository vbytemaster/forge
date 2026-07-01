# forge_objectdb

`forge_objectdb` is the declarative object/index foundation for future Forge
object databases. The first slice owns schema descriptors and deterministic
record-key layout only; it does not open databases, own transactions, or depend
on RocksDB/plugins.

## Donor Baseline

- Boost.MultiIndex: declarative `unique` / `non_unique` indices, tags and
  extractor-style member declarations.
- Boost.MultiIndex composite keys: lexicographic composite ordering and prefix
  lookup semantics.
- FoundationDB tuple layer: ordered byte encoding for composite key members.
- Chainbase/BitShares: object identity is separated into
  `{space, type, instance}` and registered deterministically.

## Object Declaration

User objects remain ordinary structs. `object<T, indexed_by<...>>` is a schema
descriptor, not a base class.

```cpp
struct account {
   forge::ids::typed_id<1, 7> id;
   std::string name;
   std::uint64_t balance;
   std::uint32_t region;
};

struct by_id;
struct by_name;
struct by_region_balance;

using account_object = forge::objectdb::object<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id, &account::id>,
      forge::objectdb::secondary_unique<by_name, &account::name>,
      forge::objectdb::secondary_non_unique<
         by_region_balance,
         forge::objectdb::composite_key<&account::region, &account::balance>>>>;
```

The primary index member must be `forge::ids::typed_id<Space, Type>`. ObjectDB
derives `object_type{space, type}` from that ID, so there is no separate table
ID or parallel object identity model.

## Modules

- `forge.objectdb.types`: shared value types such as `object_type`,
  `index_id`, `record_key` and `key_range`.
- `forge.objectdb.descriptor`: declarative object/index descriptors and traits.
- `forge.objectdb.layout`: low-level deterministic key layout for descriptor
  tests and future store/session internals.
- `forge.objectdb.cursor`: opaque key-boundary cursors and page request
  validation.
- `forge.objectdb.exceptions`: typed `forge.objectdb` errors.

## Current Boundary

This slice intentionally does not provide `store`, `session`, snapshots,
backend adapters, automatic index maintenance, revision journals or RocksDB
integration. Those layers will be designed after the descriptor/key foundation
is stable.

Application code should prefer descriptors now and future `store/session` APIs
later. `record_key` is a low-level layout primitive, not the main user-facing
object model.

## Planned Index Access Model

The next objectdb layer should expose Boost.MultiIndex-style accessors over the
declared tags. This is the API direction, not part of the current slice:

```cpp
auto by_name = session.index<account_object, by_name>();
auto alice = by_name.find("alice");

auto by_region = session.index<account_object, by_region_balance>();
auto page = by_region
   .equal_range(std::uint32_t{3})
   .page({.limit = 100});
```

The key rule is that indexed queries must execute through stored index records,
not by loading all objects or all index entries into memory and then applying
`std` algorithms.

For an ordered key/value backend such as RocksDB:

- `find` on a unique secondary index becomes an exact lookup of the encoded
  index key, followed by a primary object lookup.
- `equal_range` on a non-unique or composite index becomes an iterator seek to
  the encoded prefix, then bounded reads while keys remain inside the range.
- pagination uses opaque key-boundary cursors, not offsets.

For an in-memory backend, the same objectdb layout can be executed over an
ordered container such as `std::map<std::vector<std::byte>, bytes>`, using
`lower_bound` and range iteration. The public object/index semantics should be
the same even though the physical mechanics differ.

Queries over fields that are not declared as indexes must not be hidden behind
`find` or `equal_range`. They should be explicit scans/filters if a future layer
supports them.
