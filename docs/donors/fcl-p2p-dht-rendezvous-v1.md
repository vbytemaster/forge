# Forge P2P DHT/Rendezvous Discovery v1 Donor Note

## Scope

This note tracks the production-shaped discovery and durable-record slices now
owned by `forge_net_p2p`. Kademlia uses independent routing and record state for
the fixed Amino `/ipfs/kad/1.0.0` profile and product-owned custom profiles.
Peer/address facts remain in `peer_store`; validated values and provider
lifetimes belong to `dht::record_store`.

Supported claims stay tied to evidence. The current slice has component proof
for wire codecs, FCL-to-FCL negotiated streams, routing/provider state and
rendezvous register/discover state. The interop harness registers DHT
peer/provider lookup, Forge-Rust Rendezvous register/discover and lifecycle,
and a three-process `/pk` and `/ipns` value flow. Coordinator-owned live
execution and its artifacts are required before a registered scenario is
credited as live donor proof.

## Donor Sources

| Area | Donor source | Accepted pattern | FCL target |
|---|---|---|---|
| Kademlia DHT | `donors/libp2p-specs/kad-dht/README.md` | XOR distance over `sha256(key)`, `k=20`, `alpha=10`, bounded query timeouts and closest-peer expansion | `forge.net.p2p.dht`, `dht::routing_table`, `dht_query`, `node::async_find_peer` |
| DHT wire messages | `donors/rust-libp2p/protocols/kad/src/generated/dht.proto`, `donors/go-libp2p-kad-dht/pb/dht.proto`, `donors/go-libp2p-kad-dht/handlers.go` | Six length-delimited Protocol Buffers RPCs; `ADD_PROVIDER` is one-way and `PUT_VALUE` echoes only after accepted durable commit | `dht::codec`, `node::impl::handle_dht` |
| DHT value selection | `donors/go-libp2p-kad-dht/records.go`, `donors/rust-libp2p/protocols/kad/src/behaviour.rs` | Validate before storage, deterministic selection and quorum-bounded iterative lookup | `dht::record_store`, `node::async_put_value`, `node::async_get_value` |
| IPNS | `donors/boxo/ipns/record.go`, `donors/boxo/ipns/validation.go`, `donors/boxo/ipns/pb/record.proto` | v1/v2 signature payloads, CBOR data, expiry, public-key binding and sequence/EOL selection | `forge.net.p2p.ipns`, Amino `/ipns` policy |
| Rendezvous protocol | `donors/libp2p-specs/rendezvous/README.md` | `/rendezvous/1.0.0`, register/discover/unregister, TTL, namespace limits, cookie continuation | `forge.net.p2p.rendezvous`, `node::impl::handle_rendezvous` |
| Rendezvous wire messages | `donors/rust-libp2p/protocols/rendezvous/src/generated/rpc.proto`, `donors/rust-libp2p/protocols/rendezvous/src/codec.rs` | Proto2 message types, status codes, signed PeerRecord and cookie format | `rendezvous::codec` |

## Accepted Rules

- DHT and rendezvous mechanics live in `forge_net_p2p`, not in the official
  P2P plugin.
- Managed topology uses only explicitly configured Rendezvous points and
  namespaces. Cookies are opaque and isolated per `(point, namespace)`;
  invalid-cookie recovery clears only that pair and retries once without a
  cookie. Registration renewal follows the server-returned TTL and changes to
  the local signed PeerRecord. Shutdown unregister is best-effort and one-way,
  matching the protocol.
- Forge Peer Exchange is a Forge extension. It is queried only on identified
  peers advertising the exact Forge protocol, and third-party addresses remain
  untrusted hints until authenticated connect and Identify.
- Public API stays owner-shaped: `dht::options`, `dht::query_result`,
  `rendezvous::options`, `rendezvous::registration`, `discovery::policy`.
  The Stable legacy discovery policy is normalized into the node-owned Stage 5
  topology manager; incompatible non-default legacy and topology values are
  rejected rather than creating a second orchestrator.
- `peer_store::persistence` and `dht::record_store::persistence` are
  backend-neutral and asynchronous. The official plugin owns private ObjectDB
  adapters for both; `forge_net_p2p` has no RocksDB or DB Store dependency.
- DHT/rendezvous messages are full length-delimited libp2p protocol payloads;
  payload-only helpers are not public API.
- Every inbound DHT handler response is capped at the donor 16 KiB bound;
  non-PUT response keys are omitted and records, peers and endpoints are added
  only while the complete encoded response fits. A `PUT_VALUE` whose required
  echo cannot fit is rejected before durable mutation. Bounded inbound decoding
  retains useful candidates and skips excess peers/endpoints. The Kademlia
  shortlist begins with `k` local peers; `alpha` controls only concurrency.
- Durable provider ownership is accepted only from an authenticated
  `ADD_PROVIDER`. Third-party `GET_PROVIDERS` results remain untrusted discovery
  evidence, and stored value responses expose remaining rather than original
  TTL.
- Live support claims require matching artifacts from
  `test_forge_libp2p_interop` and do not follow from codec tests alone.

## Current Proof

| Case | Status | Proof |
|---|---|---|
| DHT protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| DHT codec and malformed rejection | Ported | `p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed` |
| DHT k-bucket bounds, replacement and XOR ordering | Ported | `p2p_dht_k_bucket_bounds_active_and_replacement_capacity`, `p2p_dht_k_bucket_closest_is_sha256_xor_ordered_and_deterministic` |
| DHT candidate admission and failure eviction | Ported | `p2p_verified_identify_dht_advertisement_admits_server_before_exchange`, `p2p_dht_query_seeds_prioritize_active_and_evict_failed_replacements` |
| DHT exact and failed-target lookup semantics | Ported | `p2p_dht_find_node_returns_exact_peer_store_and_self_before_active_closest`, `p2p_dht_query_failed_target_seed_is_not_complete_or_closest` |
| DHT node handler over negotiated stream | Ported | `p2p_dht_node_finds_peer_and_provider_over_negotiated_stream` |
| DHT iterative many-peer lookup | Ported | `p2p_dht_iterative_lookup_walks_many_peer_topology` |
| DHT iterative provider lookup and provide | Ported | `p2p_dht_iterative_provider_lookup_and_provide_reach_closest_peers` |
| DHT bounded donor materialization | Ported | `dht_amino_decoder_accepts_donor_peer_sets_beyond_outbound_k`, `p2p_dht_large_non_put_key_uses_bounded_response_and_preserves_stream`, `p2p_dht_large_stored_value_is_omitted_without_resetting_stream`, `p2p_dht_oversized_put_echo_is_rejected_before_durable_mutation`, `p2p_dht_provider_response_incrementally_truncates_to_wire_budget` |
| DHT trusted provider ownership and full fanout | Ported | `p2p_dht_get_providers_does_not_persist_third_party_claim`, `p2p_dht_fanout_full_target_attempts_every_closest_peer`, `p2p_dht_provide_replicates_to_all_closest_peers_after_quorum` |
| DHT remaining value TTL | Ported | `p2p_dht_get_value_reports_remaining_record_ttl` |
| DHT bounded async persistence | Ported | `dht_record_store_hydrates_bounded_pages_across_reopen`, `dht_record_store_enforces_value_provider_and_per_key_capacity`, `dht_record_store_enforces_record_and_total_byte_capacity` |
| DHT ObjectDB adapter reopen | Ported | `p2p_dht_record_state_mdbx_reopens_prunes_and_isolates_profiles`, `p2p_dht_record_state_rocksdb_reopens_prunes_and_isolates_profiles`; plugin lifecycle config/ownership is covered separately and is not claimed as a DHT reopen test |
| DHT live peer lookup fixture | Limited | `test_forge_libp2p_interop dht_find_peer`; direct-peer setup is not credited as outbound iterative lookup proof |
| DHT live hidden-peer FindPeer fixture | Registered | `test_forge_libp2p_interop dht_hidden_find_peer`: a fresh seeker knows only an authenticated routing seed; the hidden target is supplied only as the query key. Three Forge/Go/Rust role permutations are registered. Coordinator must publish live artifacts before this is credited as evidence. |
| DHT live provider lookup | Ported | `test_forge_libp2p_interop dht_provide_find_provider` against go-libp2p/rust-libp2p |
| DHT validated value store and bounded prune | Ported | `dht_record_store_tests`, MDBX/RocksDB reopen and expiry parity |
| DHT autonomous routing refresh | Ported | `dht_routing_refresh_tests` with fake clock, coalescing, backoff and cancellation |
| DHT owned provider lifetime | Ported | initial quorum, coalesced owners, republish snapshot, withdrawal and restart confirmation tests |
| IPNS codec, validation and selection | Ported | hardcoded Boxo v1/v2 golden, tamper, expiry, key binding and deterministic selector tests |
| DHT live `/pk` and `/ipns` values | Ported | Forge/Go/Rust `dht_pk_put_get` and `dht_ipns_put_get`: writer-only PUT, listener persistence confirmation and reader-only GET from a distinct fresh process with a reset local store |
| Rendezvous protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| Rendezvous codec, TTL, cookie and status | Ported | `p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status` |
| Rendezvous node handler over negotiated stream | Ported | `p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream` |
| Rendezvous refresh, replacement and cookie continuation | Ported | `p2p_rendezvous_refresh_replaces_registration_and_cookie_discovers_new_records` |
| Rendezvous durable registration state | Ported | async persistence fixtures and official-plugin ObjectDB reopen coverage |
| Rendezvous live register/discover | Registered | `test_forge_libp2p_interop rendezvous_register_discover` is registered only for Forge-Rust directions. Its runner requires one wire registration, valid/matching signed legacy PeerRecord, exact `forge.discovery`, positive sequence/address count, 7200-second TTLs and non-empty cookie; Forge additionally proves third-party loopback filtering. Coordinator must publish live artifacts before it is credited as evidence. |
| Rendezvous live lifecycle | Registered | `test_forge_libp2p_interop rendezvous_lifecycle` registers Forge-to-Rust and Rust-to-Forge only: legacy signed PeerRecord, cookie delta after sequence/address change, TTL renewal/expiry, unregister and final empty discovery. Coordinator must publish live artifacts before it is credited as evidence. |
| Discovery refresh feeds AutoRelay | Ported | `p2p_discovery_refresh_learns_dht_and_rendezvous_relay_candidates_for_autorelay` |

## Unsupported Gaps

- Live donor fixtures for repeated many-peer DHT/rendezvous refresh topologies
  are still limited. Forge component simulations cover the lifecycle; the
  registered hidden-peer and Forge-Rust Rendezvous scenarios await coordinator
  live artifacts.
- The `/pk` and `/ipns` matrix does not credit same-process local reads. Each
  artifact records distinct writer, listener and reader implementations and
  resets the reader store before every attempt.
- Go Rendezvous behaviour proof is not claimed because no official go-libp2p
  rendezvous behaviour donor is present in the workspace.
- Stage 5 integrates these sources into one node-owned topology manager.
  Completing that lifecycle does not promote the whole host or GossipSub to
  production readiness without the exact-head hidden-peer and donor evidence.

These gaps are also tracked in `tests/libp2p_interop/donor_cases.json`.
They must not be described as supported until matching donor-derived tests and
live artifacts are produced.

GossipSub/pubsub is tracked separately in
`docs/donors/fcl-p2p-gossipsub-v1.md` and is no longer a DHT/Rendezvous gap.
