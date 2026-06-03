# FCL Release Hardening v1

## Goal

Prepare FCL as a release-candidate foundation for the later Storlane migration pass without changing the downstream Storlane superproject or submodule pointer.

## Scope

- Split `fcl.crypto.blake2` so the public module interface contains only public declarations.
- Keep Blake2 behavior, test vectors, `yield_function_t` callback support and `blake2b_error::input_len_error` unchanged.
- Run the full FCL regression set across core, exception, raw, JSON, crypto, async runtime, app, config, schema, YAML, program options, HTTP/WebSocket, QUIC/P2P and TUI.
- Keep all release static gates green: no old `fc::` source API, no `FC_REFLECT`/`FCL_REFLECT`, no stale reflect-to-variant module, no public nested include directories and no broad root-level Boost dependency bucket.

## Blake2 Boundary

`libraries/crypto/include/fcl/crypto/blake2.cppm` is the public API:

- `fcl::bytes`;
- `fcl::blake2b_error`;
- `fcl::blake2b(...)`.

All algorithm state, constants, rotation/load helpers and compression internals live in `libraries/crypto/blake2.cpp`.

## Release Gates

Required build:

```sh
cmake -S . -B build/fcl-release-hardening-debug -G Ninja \
  -DBUILD_TESTING=ON \
  -DFCL_ENABLE_MODULES=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk

cmake --build build/fcl-release-hardening-debug -j 1 \
  --target fcl test_fcl test_fcl_exceptions test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_asio test_fcl_app test_fcl_schema test_fcl_config test_fcl_yaml \
  test_fcl_program_options test_fcl_http_websocket test_fcl_quic_p2p test_fcl_tui
```

Required tests:

```sh
ctest --test-dir build/fcl-release-hardening-debug \
  --output-on-failure \
  -R "^(test_fcl|test_fcl_exceptions|test_fcl_raw|test_fcl_json|test_fcl_crypto|test_fcl_asio|test_fcl_app|test_fcl_schema|test_fcl_config|test_fcl_yaml|test_fcl_program_options|test_fcl_http_websocket|test_fcl_quic_p2p|test_fcl_tui)$" \
  --timeout 360
```

## Storlane Migration Boundary

This pass deliberately does not:

- change the Storlane superproject;
- move the Storlane submodule pointer;
- adapt Spring contract or chain code;
- introduce source-level `fc::` compatibility aliases.

The next migration pass should consume this FCL release candidate explicitly and handle downstream contract, raw serialization and Spring compatibility at the Storlane layer.
