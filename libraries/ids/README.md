# forge_ids

`forge_ids` provides compact object identifiers shared by Forge libraries that need stable object identity without owning a database, storage backend, or application runtime.

Use it when a type needs a BitShares/Storlane-style object identity:

```cpp
import forge.ids.types;

using account_id = forge::ids::typed_id<1, 2>;

auto account = account_id{42};
auto generic = account.as_object_id(); // {space=1, type=2, instance=42}
```

## Public Modules

- `forge.ids.types` defines `forge::ids::object_id`, `typed_id<Space, Type>`, conversion helpers, string formatting, raw serialization, and variant conversion.

## Identity Model

`object_id` has three fields:

- `space`: high-level object namespace.
- `type`: object kind inside the space.
- `instance`: object instance number.

`typed_id<Space, Type>` is the strongly typed form for APIs that know the exact object kind at compile time. Convert to a generic `object_id` with `as_object_id()`, and use `try_typed<Space, Type>(...)` when decoding generic IDs.

## Serialization

Raw serialization writes fields in this exact order:

```text
space, type, instance
```

Variant conversion for `object_id` uses an object with `space`, `type`, and `instance`. Variant conversion for `typed_id<Space, Type>` keeps the compact instance-only representation.

## Boundaries

This library does not define database tables, indexes, repositories, sessions, transactions, or backend storage. Higher-level libraries can build those concepts on top of `forge_ids`, but identity stays here.
