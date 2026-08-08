# Forge Chain API

Target and package component: `forge_chain_api` / `chain_api`.

All public modules live directly in `include/forge/chain/api`. Public symbols
live in `forge::chain::api`; `info`, `block`, `state`, `transaction`,
`submission` and `admin` are API contract names, not nested namespaces. There is no aggregate
`forge.chain.api` module.

The library owns transport-neutral chain contracts, HTTP/P2P-capable clients
and proof-verification policy. Wire DTOs live in the flat
`forge.chain.protocol.{audit,info,block_query,state_query,transaction_query,admin}`
modules and `forge::chain::protocol` namespace.
API methods use existing protocol records and standard containers directly
when they already express the complete request or result.
`forge.chain.api.json_schema` tells the shared OpenAPI generator that canonical
protocol public keys and signatures are JSON strings; it owns no routes or DTOs.

`raw_client` groups ordinary info, block, state and transaction-query handles. It does
not carry the producer-administration contract; products resolve `admin`
separately only in trusted operational processes. Products also resolve the
independent `submission` contract only where transaction admission is required;
neither `raw_client` nor `transaction` can reach it. `submission_client` owns
that explicit authority and validates only that the remote acknowledgement
names the submitted transaction. That acknowledgement is not a finality claim.
Consumers establish inclusion or finality through
`verified_client::get_transaction_status` or `await_transaction`.

Every transaction submission has a positive bounded `timeout_ms`. Batch
submission uses `transaction_submit_batch_request`, whose `timeout_ms` is the
total deadline for the whole call rather than a per-item multiplier. An owner
captures that deadline once, rejects or stops when it expires, and bounds each
item by the lesser of its own timeout and the remaining batch budget. Sequential
owners must not sum item timeouts or silently continue without a deadline. HTTP
proxies allow a five-second transport grace beyond the declared application
deadline; cancellation remains typed and reaches the active request owner.

HTTP bindings are resource-oriented rather than RPC-shaped. Safe reads with
scalar path/query parameters use `GET`; structured proof ranges, simulations
and mutations use `POST`. Dynamic reads return `Cache-Control: no-store`.
Requests select an optional finalized anchor by block ID, while the complete
chain/root commitment is returned in the audited response context.
Audited requests may also provide `finality_from`, the caller's already trusted
genesis or checkpoint block. It is a proof-construction hint, not server-provided
trust: `verified_client` fills it from the installed verifier and still verifies
the returned witness locally. Recent trusted checkpoints therefore keep finality
witnesses bounded without weakening the trust bootstrap.
Table-scope continuation uses the transport-neutral protocol `bytes` value;
the HTTP GET binding carries it through the shared JSON query codec and other
transports carry the same bytes without a transport-specific cursor DTO.

Concrete controllers, state schemas, persistence, protocol publication and
network policy remain in downstream products.

Each `method_capability` advertises its own HTTP and P2P publication state;
the service never implies that every enabled method is available on every
transport. `service_limits` separately advertises page, state-batch,
transaction-batch, decoded-container allocation, transaction-status scan,
await deadline, request, response, proof and authenticated-state retention
bounds. Transports reject oversized frames, declared containers and HTTP bodies
before allocation or domain decoding. Service owners install
`limited_descriptor<Interface>(limits)` with the implementation; API Core
dispatch and the typed HTTP binding then enforce the same canonical request,
response, proof and item-count limits before and after the implementation call.
Direct in-process calls may use the typed helpers from `forge.chain.api.limits`
when they cross an untrusted boundary. Resource failures use
`forge.chain.api::exceptions::resource_exhausted` across every transport.
Capabilities and limits are operational claims made by the selected peer; they
are not consensus state and are not authenticated by a state proof. Clients use
them for feature negotiation only. `verified_client` and `submission_client`
apply independently configured local limits before dispatch and after receipt,
so a peer cannot weaken client resource policy by advertising larger values.

`verified_client` verifies transport envelopes, chain identity, finality and
generic authenticated point/range/change proofs itself. Verification returns
the authenticated source bytes, so typed composite reads can use a product
`projection_verifier` to recompute the public response from those exact bytes.
This keeps DB schema knowledge out of Forge without exposing proof internals or
trusting a server-side DTO conversion. Missing projection support fails closed with
`audit_not_supported`; it never falls back to an envelope-only check.
Public client, verifier and authenticated-store boundaries preserve existing
Forge exceptions and translate implementation `std`/Boost failures into their
own typed Forge error contracts. Asio operation cancellation remains a typed
`forge::asio::exceptions::canceled` result rather than an availability error.

## Verified state synchronization

`state_changes_request::from_block` is exclusive and `to_block` is inclusive.
Its cursor identifies the next block, requested range and key within that
range. A verified page must start exactly at that position, cover blocks and
ranges in canonical order, and either return that exact continuation or end at
the finalized `to_block` anchor. Every returned range has its own authenticated
change-tree proof.

Change proofs establish the complete mutation set for each requested range;
the finality verifier also proves that every returned intermediate anchor is a
canonical ancestor of the finalized target. They do not by themselves prove a
state transition. A partial-state consumer
keeps the authenticated frontier obtained from its snapshot, applies verified
changes in order and accepts the update only when the resulting root equals the
finalized target `state_root`. This is the intended `mountd`-class client path
and prevents a server from omitting an intermediate mutation or inventing an
unbound intermediate root.

## Local ABI conversion

`forge.chain.api.abi` owns local `forge::variant` JSON-value conversion for
`forge::chain::protocol::abi_def`. `abi_json_to_bin` and `abi_bin_to_json`
perform no network calls and require an explicit ABI and root type on every
invocation. The decoder rejects trailing bytes.

The traversal and wire rules are adapted from Spring commit `e6a99f68`,
`libraries/chain/abi_serializer.cpp` and
`libraries/chain/include/eosio/chain/abi_serializer.hpp`. Supported ABI shapes
include built-in Spring types, typedefs, struct inheritance, dynamic and fixed
arrays, optional fields, trailing binary extensions and tagged variants.
Public Forge names and error contracts are native; there are no EOSIO namespace
aliases or compatibility modes.

`abi_serialization_limits` bounds recursion depth, elapsed serialization time,
binary bytes, string/byte payloads and container elements. Failures throw
`abi_serialization_error` with a structured `abi_diagnostic` containing a
stable code, message, resolved type, logical JSON path and binary offset.
