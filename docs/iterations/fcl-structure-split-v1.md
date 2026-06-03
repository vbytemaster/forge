# FCL Structure Split v1

## Summary

This pass moves the codebase from the old source layout into domain-owned FCL libraries.

The pass is intentionally source-breaking: public APIs move to `libraries/<lib>/include/fcl/...`, namespaces become `fcl`, and CMake targets become `fcl_*`. The raw binary layout remains the compatibility boundary.

## Layout

FCL-owned libraries use this shape:

```text
libraries/<lib>/
  CMakeLists.txt
  include/fcl/<lib>/*.cppm
  *.cpp
  *_private.hpp
```

Third-party source and submodules live only under root `vendor/`.

## Domain Targets

- `fcl_core`: scalar helpers, time value types, string, utf8, bit/byte helpers and small utility helpers.
- `fcl_exceptions`: exception and capture helpers.
- `fcl_reflect`: reflection helper utilities.
- `fcl_variant`: variant, variant object, static variant, dynamic bitset.
- `fcl_raw`: raw pack/unpack, raw datastream and varint compatibility helpers.
- `fcl_json`: JSON parser/emitter compatibility behavior.
- `fcl_log`: logger, log message and console appender.
- `fcl_crypto`: hashes, AES, K1/R1/WebAuthn/BLS, base encodings, random and key wrappers.
- `fcl`: umbrella static target assembled from the domain targets.

## Follow-Up Result

This split exposed old monolithic FC coupling that is not fully solved by moving files. The intended state is not “all modules owned by one shared CMake module target”; each domain target should own its own module interfaces.

The temporary shared BMI-producer target has since been removed. `fcl_modules` must not return.

The follow-up dependency cleanup established:

- build a documented acyclic target graph;
- split `exception`/`log` and `variant`/`json`/`reflect` coupling where needed;
- keep `core` below all higher domains;
- move `FILE_SET CXX_MODULES` ownership to the corresponding `fcl_*` targets;
- keep the raw byte-compatibility tests green.

## Non-Goals

- The Boost.Describe rewrite is handled by the next pass.
- No semantic rewrite of raw serialization.
- No crypto pruning.
- No new network layer.
- No new YAML/schema/logging design.

## Acceptance

- Root `include/`, `src/`, `test/` and old top-level third-party paths are gone from tracked files.
- `libraries/` has no nested `src/` or `vendor/` directories.
- The removed legacy network, GELF, DMLOG, zlib, old persistence and old container zones do not reappear.
- `test_fcl` keeps the old behavioral coverage under the new `fcl` API.
- A small raw golden test proves representative bytes stay stable across the rename.
