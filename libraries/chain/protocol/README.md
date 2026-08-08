# forge_chain_protocol

`forge_chain_protocol` provides canonical blockchain protocol values and wire
records. Use it to build, serialize, hash and sign Forge-compatible actions,
transactions and blocks. It builds on `forge_chain_core` without adding
controller, execution or node runtime behavior.

The flat `forge.chain.protocol.audit`, `info`, `block_query`, `state_query`,
`transaction_query` and `admin` modules own the transport-neutral chain wire
contract. API classes, clients and transport bindings remain in
`forge_chain_api`; wire DTOs never live beside those runtime interfaces.
Table-scope pagination carries opaque authenticated keys as `bytes` in
`table_scope_request::cursor` and `table_scope_response::next`; callers must
not parse or reconstruct them from contract scope names.

Package component: `chain_protocol`. Public namespace:
`forge::chain::protocol`.

## Modules

- `forge.chain.protocol.types`: digest aliases, names, assets, symbols, IDs,
  keys, signatures, timestamps and scalar wire vocabulary.
- `forge.chain.protocol.fixed_key`: `fixed_key<Size>` and `key256`, with
  ordered word construction, canonical raw bytes and fixed-width hex variants.
- `forge.chain.protocol.authority`: permission weights and authority thresholds.
- `forge.chain.protocol.producer_schedule`: legacy producer keys and schedules
  used by block headers and contract APIs.
- `forge.chain.protocol.producer_authority`: weighted block-signing authorities
  and producer authority schedules.
- `forge.chain.protocol.finalizer_authority`: finalizer identity, weight and
  public-key bytes.
- `forge.chain.protocol.finalizer_policy`: weighted finalizer policy records.
- `forge.chain.protocol.action`: actions and the Savanna action digest that
  commits to the return value.
- `forge.chain.protocol.action_receipt`: canonical action receipts, witness
  hashes and Savanna receipt digests.
- `forge.chain.protocol.transaction`: transactions, packed transactions,
  compression, IDs and signing digests. It re-exports the action module.
- `forge.chain.protocol.transaction_trace`: typed transaction and action
  execution traces shared by runtimes, block metadata and Chain API clients.
- `forge.chain.protocol.block`: block headers, receipts, signed blocks, block
  IDs and transaction receipt Merkle roots.
- `forge.chain.protocol.abi`: ABI records and optional tail-field decoding.
- `forge.chain.protocol.system`: canonical system action payloads.
- `forge.chain.protocol.audit`, `info`, `block_query`, `state_query`,
  `transaction_query`, `admin`: common audit envelopes and endpoint wire
  records for the transport-neutral Chain API.

The target publicly links `forge_chain_core`, `forge_compression`, `forge_raw`,
`forge_variant`, `forge_crypto_asymmetric_values`, `forge_crypto_asymmetric`
and `forge_crypto_digest`.

Authority, producer schedule, producer authority and finalizer policy modules
share guest-safe value partitions with host wrappers. Host wrappers provide the
same full Raw and Variant serialization contract; `block_signing_authority`
uses the Spring/FC `[index, payload]` JSON representation.

`forge::chain::protocol::digest` aliases `forge::chain::core::digest`. Protocol
names such as `chain_id`, `block_id`, `transaction_id` and `checksum256` remain
in the protocol namespace while sharing the same SHA-256 representation.

## Fixed-Size Keys

```cpp
#include <cstdint>

import forge.chain.protocol.fixed_key;

const auto key =
   forge::chain::protocol::key256::make_from_word_sequence<std::uint64_t>(
      0, 0, 0, 42);
const auto bytes = key.extract_as_byte_array();
```

Raw serialization uses the canonical fixed-width byte sequence. Variant
conversion uses fixed-width hexadecimal text. Ordering follows the stored word
representation and is covered by compatibility fixtures.

## Transactions And Blocks

```cpp
#include <chrono>
#include <deque>

import forge.chain.protocol.block;

auto transaction = forge::chain::protocol::transaction{};
transaction.expiration = std::chrono::sys_seconds{};

auto receipts = std::deque<forge::chain::protocol::transaction_receipt>{};
auto header = forge::chain::protocol::block_header{};
header.transaction_mroot =
   forge::chain::protocol::calculate_transaction_mroot(receipts);
```

Protocol declarations preserve field order and canonical raw encoding.
Transaction IDs, signing digests, block IDs and receipt digests must be computed
with the provided helpers over canonical raw representations.

## Savanna Action Receipts

```cpp
#include <array>

import forge.chain.core.merkle;
import forge.chain.protocol.action_receipt;

auto receipt = forge::chain::protocol::action_receipt{};
receipt.receiver = executed_action.account;
receipt.act_digest = forge::chain::protocol::generate_action_digest(
   executed_action, return_value);

const auto leaf = forge::chain::protocol::calculate_savanna_action_digest(
   receipt, executed_action);
const auto action_root = forge::chain::core::calculate_merkle_root(
   std::array{leaf});
```

The Blockchain execution runtime owns sequence allocation and receipt
construction. Forge owns the receipt wire record and deterministic hashing.
The Merkle root above is the root of executed action receipt digests used by
Savanna finality data. In a proper Savanna block, the historical
`block_header.action_mroot` field carries a finality-tree root claim instead;
the two roots must not be substituted for one another.

## Boundaries And Safety

Text and variant conversion are API and diagnostic representations, not signing
preimages. Parse untrusted keys, signatures, compressed transactions and raw
payloads through the typed Forge codecs and handle their typed failures. Private
key custody and signing authorization belong to the product runtime.

This library does not provide state storage, controller behavior, transaction
execution, action trace collection, sequence allocation, consensus, finality,
P2P synchronization, block production, Merkle proofs or finality-tree
construction.

## Tests

`test_forge_chain_protocol` covers raw and variant fixtures, fixed keys, names
and assets, transactions, compression, signatures, ABI compatibility, block
IDs, transaction receipt roots and Spring-compatible Savanna action receipts.
`test_forge_package_chain_protocol_component` verifies the installed
`chain_protocol` component, protocol imports and the transitive core digest
dependency.
