# Forge DB authenticated proof donor manifest v1

This manifest records proof-security evidence only. Forge owns its hash schema,
DTOs, binary codec and ranked range verifier; donor formats are not product
dependencies.

## Sources

| Source | Pin | License | Inspected evidence |
|---|---|---|---|
| `cosmos/ics23` | Go verifier commit `7f2c2d0965fdcf33658cce3198ddae078a449fc2`; IAVL fixtures from tag `v0.7.1`, commit `014bd93b66bb57e5f250be0c9a344505f7d0fa70` | Apache-2.0 | Official IAVL existence and non-existence vectors plus `VerifyMembership` and `VerifyNonMembership` behavior |
| `cosmos/iavl` Dragonfruit fix | Release `v0.19.3`, commit `7f698ba3fa232c54109e5b4ea42562bbecdb1bf8` | Apache-2.0 | `proof.go` ambiguity fix and `proof_forgery_test.go` regression: a proof inner node must not accept simultaneous left and right child hashes |
| `cosmos/iavl` versioned tree | Commit `0b9d3dab675013cef9ddc8a93ed7728caeccbae4` | Apache-2.0 | mutable/immutable version roots, historical reads, delete-version/pruning behavior and crash-safe committed-version expectations |
| `aptos-labs/aptos-core` Jellyfish Merkle restore | Commit `bc5690f6735a4adc9360ee814cc1b0c6148aeb38`, `storage/jellyfish-merkle/src/restore` | Aptos source license | ordered chunk restore, expected-root binding, overlap/restart handling and recovery from the rightmost persisted leaf |
| Cosmos security advisory | `Cosmos-SDK Security Advisory Dragonfruit`, 2022-10-08 | Documentation reference | High-severity warning for legacy IAVL `RangeProof` and recommendation to avoid that native proof format |

Upstream references:

- <https://github.com/cosmos/ics23>
- <https://github.com/cosmos/iavl/commit/7f698ba3fa232c54109e5b4ea42562bbecdb1bf8>
- <https://github.com/cosmos/iavl/tree/0b9d3dab675013cef9ddc8a93ed7728caeccbae4>
- <https://github.com/aptos-labs/aptos-core/tree/bc5690f6735a4adc9360ee814cc1b0c6148aeb38/storage/jellyfish-merkle/src/restore>
- <https://forum.cosmos.network/t/cosmos-sdk-security-advisory-dragonfruit/7614>

## Decisions

Accepted patterns:

- independent membership and non-membership verification against fixed roots;
- exactly one typed sibling per point-proof step, making the Dragonfruit
  simultaneous-left-and-right state structurally unrepresentable;
- exact root and tree-size binding before accepting point or range results;
- complete preorder consumption, strict key ordering, authenticated subtree
  sizes and ranks, and explicit lower/upper boundary witnesses;
- malformed, omitted, duplicated and reordered witness nodes as mandatory
  negative coverage.
- version roots are immutable snapshots; pruning removes server-side history
  without invalidating a proof already issued against an authenticated root;
- restart recovery accepts only a complete committed version or the prior
  committed version, never a mixed root/node/value/Revision state;
- snapshot import consumes a strictly ordered stream bound to one expected root,
  and a restarted importer resumes only from its persisted ordered frontier.

Rejected patterns:

- legacy IAVL `RangeProof` DTOs or verification behavior;
- relabelling Forge proof JSON, raw bytes or hash schema as ICS23;
- using the official Go verifier as a Forge product runtime dependency;
- accepting a reconstructed root without separately proving requested range
  continuity and certified boundaries.

## Forge evidence

- [Official ICS23 and independent Forge Go lanes](../../tests/db_authenticated/ics23_harness/README.md)
- [Forge point membership vector](../../tests/db_authenticated/ics23_harness/vectors/forge_point_membership_v3.json)
- [Forge point non-membership vector](../../tests/db_authenticated/ics23_harness/vectors/forge_point_nonmembership_v3.json)
- [Native golden and ranked-range adversarial tests](../../tests/db_authenticated/ranked_range_proof_adversarial_tests.cpp)
- [Version, pruning, restart and process-crash tests](../../tests/db_authenticated/db_authenticated_tests.cpp)
- [Authenticated transaction crash helper](../../tests/db_authenticated/authenticated_crash_helper.cpp)

The ranked-range corpus rejects malformed metadata, omitted nodes, duplicated
nodes, reordered leaves, an omitted lower predecessor and an omitted upper
boundary while preserving a positive root/rank baseline.

The native history corpus also keeps a proof from a subsequently pruned version
and verifies it after server-side root and garbage records have been removed.
The subprocess lane kills a durable MDBX writer both before and after the shared
Revision/authenticated transaction commit and verifies the recovered root,
value proof and Revision head.

## Limits

- The independent Go verifier covers Forge v3 state-tree point proofs only. It
  does not decode Forge raw proof bytes or verify ranked range/change proofs.
- The official ICS23 lane verifies official IAVL protobuf vectors only. Forge
  still has no native ICS23 codec or verifier and claims no ICS23 wire
  compatibility.
- These tests reduce implementation variance; they do not replace the external
  cryptographic review required before production use.
