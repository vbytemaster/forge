# FCL Modules + Boost.Describe v1

## Summary

This pass makes FCL module-first and removes the transitional reflection macro layer. Public entrypoints live as C++ module files under `libraries/<lib>/include/fcl/<lib>/.../*.cppm`; implementation details live inside each library.

The compatibility boundary is binary: FCL raw serialization must keep the old FC raw byte layout for described types, enums, variants, containers and crypto wrappers.

## Decisions

- Boost.Describe is the canonical reflection spelling for this iteration.
- No FCL reflection macro wrappers are introduced in this pass.
- `fcl::reflect` is a thin utility layer over Boost.Describe for described type detection, member traversal, base-first traversal and enum conversion.
- Schema, validation, required fields, ranges and redaction are future layers; Boost.Describe only supplies metadata.
- Public textual headers were moved out of public `include/fcl`; public entrypoints are real module files.
- Public declarations in module files use explicit `export namespace fcl... { ... }`. Broad `export { ... }` blocks are not accepted because they hide what is part of the API and can accidentally export private implementation details.
- Module and implementation files use C++17 nested namespace spelling, for example `namespace fcl::raw { ... }`, not legacy nested namespace blocks.
- Macro-only `.hpp` files are the only public textual exception, because C++ modules cannot export macros. These files may define capture/log/assert helper macros, but must not contain public types, templates, functions or old API declarations.
- The earlier wrapper model where `.cppm` included `private/fcl/.../*.hpp` was a transitional mistake and is not an accepted FCL layout.
- Internal tests and implementation may include root-level implementation-only private headers, but module entrypoints must not be header wrappers.

## Raw Compatibility

For every old reflected field order, the new Boost.Describe declaration must keep the same member order. Raw pack/unpack traverses described base members first and then local members, matching the old derived-type byte order.

Golden tests cover primitives, strings, containers, optionals, described structs, derived structs and described enums. Crypto tests remain enabled.

## Layout

```text
libraries/<lib>/
  CMakeLists.txt
  include/fcl/<lib>/*.cppm
  include/fcl/<lib>/*macros.hpp   # macro-only textual exceptions, if needed
  *.cpp
  *_private.hpp

tests/<lib>/*.cpp
vendor/utf8cpp/
```

`libraries/` must not contain nested `tests`, `src`, `vendor`, `utf8` or `private/fcl` directories. Public include paths must not contain nested directories below `include/fcl/<lib>`.

## Known Debt: Module Ownership And Dependency Graph

The old migration build used a shared `fcl_modules` target to produce C++ module BMI files. `BMI` means Binary Module Interface: the compiler-generated artifact required before another translation unit can `import` a module.

This was a migration bridge, not the target architecture. The source layout and public API are domain-shaped, and module ownership now lives on the domain targets. The old FC monolith exposed several dependency knots that had to be broken:

- `variant` no longer depends on `exception` or `json`.
- `reflect` no longer depends on `exception`.
- `json` no longer depends on `log`.
- `websocket` no longer depends on `http`.
- `crypto` no longer imports logger from low-level crypto modules.

The accepted state is a domain target graph with no reverse dependencies. Current important edges:

```text
fcl_variant -> fcl_core
fcl_reflect -> fcl_core, fcl_variant
fcl_log -> fcl_core, fcl_reflect, fcl_variant
fcl_exceptions -> fcl_core, fcl_log, fcl_variant
fcl_raw -> fcl_core, fcl_exceptions, fcl_reflect, fcl_variant
fcl_json -> fcl_core, fcl_exceptions, fcl_variant
fcl_crypto -> fcl_core, fcl_exceptions, fcl_raw, fcl_reflect, fcl_variant
```

The exact graph may differ after cleanup, but it must be explicit and acyclic. If a domain needs reverse imports, split the domain rather than hiding the cycle behind an umbrella target. `FILE_SET CXX_MODULES` ownership now belongs to the real domain targets; a shared BMI pool must not return.

## Toolchain

`FCL_ENABLE_MODULES=ON` requires a module-capable LLVM toolchain. AppleClang is not a supported module toolchain for this build and fails during configuration with an explicit error.

## Non-goals

- No schema or validation layer.
- No YAML redesign.
- No source-level compatibility with old FC namespaces or reflection macros.
- No crypto pruning.
