# forge_objectdb

`forge_objectdb` is a neutral object/index engine foundation for Forge. It
describes typed objects, maintains declared primary and secondary indexes, and
executes async object/index queries over a caller-provided record session.

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

## Session Boundary

`forge::objectdb::session` is the public virtual record-session contract used by
the object engine:

```cpp
class session {
public:
   virtual ~session() = default;

   virtual boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get(record_key key) = 0;

   virtual boost::asio::awaitable<void>
   put(record_key key, std::vector<std::byte> value) = 0;

   virtual boost::asio::awaitable<void>
   erase(record_key key) = 0;

   virtual boost::asio::awaitable<record_scan_result>
   scan_page(key_range range, page_request page) = 0;

   virtual boost::asio::awaitable<void> commit() = 0;
   virtual boost::asio::awaitable<void> rollback() = 0;
};
```

`session` is intentionally record-oriented. Ordinary users should work with
`store` and `transaction`; backend adapters implement this session contract.
`forge_objectdb` does not import `forge_rocksdb`, plugins, app lifecycle or
product code.

`session_factory<Session>` wraps a callable that opens one fresh backend session
per transaction:

```cpp
forge::objectdb::session_factory<my_session> factory{[]() -> boost::asio::awaitable<std::unique_ptr<my_session>> {
   co_return std::make_unique<my_session>();
}};
```

## Store And Transaction

`forge::objectdb::store` is non-templated. It receives a session factory and
registers object descriptors at runtime:

```cpp
forge::objectdb::store store{factory};
store.register_object<account_object>();

auto tx = co_await store.begin_transaction();

auto account = co_await tx.get(account::id_type{42});
auto maybe = co_await tx.find(account::id_type{42});
co_await tx.erase(account::id_type{42});

auto runtime_loaded = co_await tx.get<account_object>(
   forge::ids::object_id{.space = 1, .type = 7, .instance = 42});

co_await tx.commit();
```

Typed-id overloads use the macro mapping and return the correct object type.
Runtime `forge::ids::object_id` overloads require explicit `<Object>` and reject
mismatched `{space, type}`.

Every mutation uses transaction semantics. Direct `store` methods open a
short-lived transaction, delegate to the same mutation pipeline, commit on
success, and rollback on failure:

```cpp
co_await store.insert(account{...});
co_await store.replace(account{...});
co_await store.modify(account::id_type{42}, [](account& value) {
   value.balance += 100;
});
co_await store.erase(account::id_type{42});

auto value = co_await store.get(account::id_type{42});
auto maybe = co_await store.find(account::id_type{42});
auto alice = co_await store.index<account_object, by_name>().find("alice");
```

Use an explicit transaction when several object mutations must commit or
rollback together:

```cpp
auto tx = co_await store.begin_transaction();
co_await tx.insert(account{...});
co_await tx.modify(account::id_type{42}, [](account& value) {
   value.region = 3;
});
auto alice = co_await tx.index<account_object, by_name>().find("alice");
co_await tx.commit();
```

`insert`, `replace`, `modify` and `erase` maintain primary and secondary records
atomically within the backend session. Unique secondary conflicts throw typed
`forge.objectdb` exceptions. If a transaction is destroyed without `commit()`,
its backend session is rolled back best-effort and uncommitted object/index
records are not persisted.

## Index Access

Index access is Boost.MultiIndex-style:

```cpp
co_await tx.insert(account{...});
co_await tx.replace(account{...});

auto alice = co_await tx.index<account_object, by_name>().find("alice");

auto page = co_await tx.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .page({.limit = 100});

auto exact = co_await tx.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}, std::uint64_t{100}))
   .page({.limit = 100});
```

Composite lookup supports `std::make_tuple(...)` for full keys and partial
prefixes. Variadic overloads delegate to the tuple path.

Large result sets can be consumed lazily:

```cpp
auto stream = store.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .stream({.page_size = 100});

while (auto item = co_await stream.next()) {
   // item is account
}

co_await store.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .for_each({.page_size = 100}, [](const account& value) -> boost::asio::awaitable<void> {
      co_return;
   });
```

Queries execute through declared index records. `find` and `equal_range` do not
load a whole table or whole index into memory and then apply `std` algorithms.

## Hooks

`store` supports a small hook layer around committed mutations:

- interceptors run before a mutation writes records and may veto with a typed
  exception;
- observers run after successful commit with a `change_set`;
- observers are not called for rollback or failed commit.

The hook layer is intentionally objectdb-level. It observes object mutations and
does not expose backend-specific write batches or RocksDB handles.

## Backend Boundary

Objectdb owns object/index semantics; the caller owns opening, closing,
flushing, scheduling, retry policy and concrete persistence. The test suite
proves the same store behavior against an ordered in-memory session and a
RocksDB-backed session.

`forge_objectdb_rocksdb` is the optional RocksDB adapter component. It lives
outside `forge_objectdb` so the core engine remains backend-free:

```cpp
forge::objectdb::rocksdb::driver rocks{
   forge::objectdb::rocksdb::config{
      .path = "data/rocksdb/witness",
      .family = "objectdb"
   }
};

forge::objectdb::store store{rocks.session_factory()};
store.register_object<account_object>();
```

## Future Plugin Model

The future `plugins.db.objectdb` plugin should own app lifecycle and named
stores. It is not implemented in this block.

```yaml
plugins:
  db:
    objectdb:
      stores:
        - name: witness
          driver: rocksdb
          path: "./data/rocksdb/witness"
          write-policy: single-writer
```

A named store is a physical/lifecycle boundary. The `{space, type, instance}`
ID remains object identity inside that store.

## Modules

- `forge.objectdb.types`: shared value types and low-level record keys.
- `forge.objectdb.descriptor`: base object and object/index descriptors.
- `forge.objectdb.layout`: deterministic ordered key layout.
- `forge.objectdb.cursor`: opaque cursor and page request validation.
- `forge.objectdb.store`: session contract, session factory, async store,
  transaction, index view, range query and stream APIs.
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
