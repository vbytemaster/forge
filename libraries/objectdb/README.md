# forge_objectdb

`forge_objectdb` is a neutral object/index engine foundation for Forge. It
describes typed objects, maintains declared primary and secondary indexes, and
executes async object/index queries over a caller-provided storage context.

It is not a RocksDB wrapper, app plugin, blockchain database, FUSE layer or
product policy surface.

## Donor Baseline

- BitShares/Graphene/Chainbase and EOSIO: base object identity, `add_index<T>()`
  style registration and deterministic object-id mapping.
- Boost.MultiIndex: index tags, unique/non-unique indexes, extractor-style
  declarations, `find`, `lower_bound`, `upper_bound` and `equal_range`.
- Boost.MultiIndex composite keys: lexicographic composite ordering and partial
  prefix lookup through tuple-like values.
- FoundationDB tuple layer: stable ordered byte encoding for composite keys and
  cursor/range boundaries.
- RocksDB/Pebble: exact lookup, iterator seek and bounded persisted index scans.
- PostgreSQL/LMDB: migration, snapshot and concurrency concepts stay explicit
  future blocks, not hidden behavior in this first store slice.

## Object Declaration

Objects derive from `forge::objectdb::object<Derived, Space, Type>`. The base
owns `id` as `forge::ids::typed_id<Space, Type>` and contains no storage
behavior.

```cpp
struct account : forge::objectdb::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;
   std::uint32_t region = 0;
};

BOOST_DESCRIBE_STRUCT(account, (forge::objectdb::object<account, 1, 7>), (name, balance, region))

struct by_id;
struct by_name;
struct by_region_balance;

using account_object = forge::objectdb::object_index<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id>,
      forge::objectdb::secondary_unique<by_name, &account::name>,
      forge::objectdb::secondary_non_unique<
         by_region_balance,
         forge::objectdb::composite_key<&account::region, &account::balance>>>>;

FORGE_OBJECTDB_OBJECT(account_object)
```

`object_index<T, indexed_by<...>>` is the schema descriptor. User objects remain
ordinary described structs. `FORGE_OBJECTDB_OBJECT(...)` creates the inverse
compile-time mapping from `typed_id<Space, Type>` to the object descriptor, so
typed-id operations do not require spelling the object type again.

## Store And Session

`forge::objectdb::store` is non-templated. It receives a caller-provided async
storage context and registers object descriptors at runtime:

```cpp
forge::objectdb::store store{storage};
store.register_object<account_object>();

auto session = co_await store.session();

auto account = co_await session.get(account::id_type{42});
auto maybe = co_await session.find(account::id_type{42});
co_await session.erase(account::id_type{42});

auto runtime_loaded = co_await session.get<account_object>(
   forge::ids::object_id{.space = 1, .type = 7, .instance = 42});
```

Typed-id overloads use the macro mapping and return the correct object type.
Runtime `forge::ids::object_id` overloads require explicit `<Object>` and reject
mismatched `{space, type}`.

`insert`, `replace` and `erase` maintain primary and secondary records
atomically within the storage session contract. Unique secondary conflicts throw
typed `forge.objectdb` exceptions.

## Index Access

Index access is Boost.MultiIndex-style:

```cpp
co_await session.insert(account{...});
co_await session.replace(account{...});

auto alice = co_await session.index<account_object, by_name>().find("alice");

auto page = co_await session.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .page({.limit = 100});

auto exact = co_await session.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}, std::uint64_t{100}))
   .page({.limit = 100});
```

Composite lookup supports `std::make_tuple(...)` for full keys and partial
prefixes. Variadic overloads delegate to the tuple path.

Large result sets can be consumed lazily:

```cpp
auto stream = session.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .stream({.page_size = 100});

while (auto item = co_await stream.next()) {
   // item is account
}

co_await session.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .for_each({.page_size = 100}, [](const account& value) -> boost::asio::awaitable<void> {
      co_return;
   });
```

Queries execute through declared index records. `find` and `equal_range` do not
load a whole table or whole index into memory and then apply `std` algorithms.

## Storage Context Boundary

The store accepts a storage context instead of importing `forge_rocksdb`,
plugins, app lifecycle or product code. The context supplies awaitable session
operations for objectdb records:

- `get(record_key)`;
- `put(record_key, bytes)`;
- `erase(record_key)`;
- `scan_page(key_range, page_request)`;
- `commit()` / `rollback()`.

This is an execution boundary, not a public backend framework. Objectdb owns the
object/index semantics; the caller owns opening, closing, flushing, scheduling,
retry policy and concrete persistence.

The test suite proves the same object/index behavior against an ordered
in-memory context and a RocksDB-backed context.

## Modules

- `forge.objectdb.types`: shared value types and low-level record keys.
- `forge.objectdb.descriptor`: base object and object/index descriptors.
- `forge.objectdb.layout`: deterministic ordered key layout.
- `forge.objectdb.cursor`: opaque cursor and page request validation.
- `forge.objectdb.store`: async store, session, index view, range query and
  stream APIs.
- `forge.objectdb.exceptions`: typed `forge.objectdb` errors.
- `<forge/objectdb/macros.hpp>`: macro-only object-id mapping declaration.

## Migration Groundwork

Runtime migration/catalog support is intentionally out of this block. These
changes are migration events and must be handled by a future explicit migration
layer:

- changing `{space, type}`;
- changing index order or index kind;
- changing an extractor or composite-key member order;
- changing base object serialization;
- changing the ordered key codec.

The current rule is simple: descriptors and key layout are deterministic and
tested; migration policy is not hidden inside the store.
