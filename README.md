# storlane-fc

`storlane-fc` is a donor-derived standalone extraction of `spring/libraries/libfc`
from [AntelopeIO/spring](https://github.com/AntelopeIO/spring).

## Provenance

- donor repository: `AntelopeIO/spring`
- donor subtree: `libraries/libfc`
- donor baseline commit: `e6a99f68b67abc4d89fe716755b2e1394a4991f7`

The goal of this repository is to keep `fc` consumable as an independent CMake
project and as a submodule under `storlane/vendor/fc`, while staying close to the
upstream donor layout and update path.

## Nested submodules

This repository keeps the donor topology with nested submodules:

- `include/fc/crypto/webauthn_json`
- `secp256k1/secp256k1`
- `libraries/bn256`
- `libraries/bls12-381`

Clone and update recursively:

```bash
git clone https://github.com/vbytemaster/storlane-fc.git
cd storlane-fc
git submodule update --init --recursive
```

## Build

Example configure on macOS with the `Storlane` LLVM/Boost baseline:

```bash
cmake -G Ninja -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  -DBOOST_ROOT=$HOME/.openclaw/toolchains/boost/1.90.0 \
  -DBOOST_INCLUDEDIR=$HOME/.openclaw/toolchains/boost/1.90.0/include \
  -DBOOST_LIBRARYDIR=$HOME/.openclaw/toolchains/boost/1.90.0/lib \
  -DBoost_DIR=$HOME/.openclaw/toolchains/boost/1.90.0/lib/cmake/Boost-1.90.0 \
  -DBoost_NO_SYSTEM_PATHS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Update policy

`storlane-fc` should be updated from upstream `Spring/libfc` intentionally and with
an explicit donor baseline bump. Avoid opportunistic refactors that make future
syncs harder unless they are required to keep the library standalone or consumable
from `Storlane`.
