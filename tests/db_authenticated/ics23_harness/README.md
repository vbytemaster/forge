# Authenticated proof cross-language harness

This isolated Go module owns two distinct test lanes. It is not linked into
Forge and adds no product runtime dependency.

## Official ICS23 donor lane

`vectors_test.go` verifies copied upstream IAVL vectors with the official
`github.com/cosmos/ics23/go` implementation.

The verifier dependency is pinned to upstream commit
`7f2c2d0965fdcf33658cce3198ddae078a449fc2` through its Go pseudo-version.

Upstream source: `cosmos/ics23` tag `v0.7.1`, commit
`014bd93b66bb57e5f250be0c9a344505f7d0fa70`:

- `testdata/iavl/exist_middle.json`
- `testdata/iavl/nonexist_middle.json`

The copied fixtures retain their upstream Apache-2.0 provenance.

This lane establishes the behavior of the official ICS23 verifier. It does not
assert that Forge authenticated proof DTOs, binary encoding or hash schema are
ICS23-compatible.

## Forge v3 point lane

`forge_vectors_test.go` is an independent standard-library Go verifier for the
documented Forge hash schema v3 point-proof semantics. It validates:

- `vectors/forge_point_membership_v3.json`;
- `vectors/forge_point_nonmembership_v3.json`.

The verifier independently reconstructs value, leaf and inner hashes, validates
ordered path metadata and search direction, binds the expected state root and
size, and checks the resulting rank. The matching C++ golden test lives in
`../ranked_range_proof_adversarial_tests.cpp`.

The Forge JSON vector format is test interchange only. It is not the Forge raw
binary proof encoding, an ICS23 protobuf message or an ICS23 `ProofSpec`.

## Running

Run both lanes explicitly from this directory:

```sh
GOWORK=off go test -mod=readonly ./...
```

They can also be registered with CTest by configuring a disposable build with
`FORGE_DB_AUTHENTICATED_ENABLE_GO_VECTOR_TESTS=ON`. The option is off by default,
so the normal Forge CMake build neither downloads nor builds Go/protobuf
dependencies.
