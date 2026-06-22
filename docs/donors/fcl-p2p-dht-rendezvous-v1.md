# FCL P2P DHT/Rendezvous Discovery v1 Donor Note

## Scope

This note tracks the first production-shaped discovery slice in `fcl_p2p`.
The implementation adds owner modules for Kademlia-compatible DHT and
libp2p rendezvous, durable state through `peer_store`, node-level protocol
handlers selected through multistream-select, and the F.2 discovery lifecycle
hardening pass: iterative DHT lookup, provider publication to discovered
closest peers, rendezvous refresh/cookie semantics and discovery-backed
AutoRelay candidate learning.

Supported claims stay tied to evidence. The current slice has component proof
for wire codecs, FCL-to-FCL negotiated streams, routing/provider state and
rendezvous register/discover state, plus live donor interop artifacts for
DHT peer/provider lookup against go-libp2p and rust-libp2p and Rendezvous
register/discover against rust-libp2p.

## Donor Sources

| Area | Donor source | Accepted pattern | FCL target |
|---|---|---|---|
| Kademlia DHT | `donors/libp2p-specs/kad-dht/README.md` | XOR distance over `sha256(key)`, `k=20`, `alpha=10`, bounded query timeouts and closest-peer expansion | `fcl.p2p.dht`, `dht::routing_table`, `dht_query`, `node::async_find_peer` |
| DHT wire messages | `donors/rust-libp2p/protocols/kad/src/generated/dht.proto`, `donors/go-libp2p-kad-dht/pb/dht.proto`, `donors/go-libp2p-kad-dht/handlers.go` | Length-delimited Protocol Buffers message with `FIND_NODE`, `ADD_PROVIDER`, `GET_PROVIDERS`; `ADD_PROVIDER` is send-message and validates provider peer equals stream peer | `dht::codec`, `node::impl::handle_dht`, `node::async_provide` |
| Rendezvous protocol | `donors/libp2p-specs/rendezvous/README.md` | `/rendezvous/1.0.0`, register/discover/unregister, TTL, namespace limits, cookie continuation | `fcl.p2p.rendezvous`, `node::impl::handle_rendezvous` |
| Rendezvous wire messages | `donors/rust-libp2p/protocols/rendezvous/src/generated/rpc.proto`, `donors/rust-libp2p/protocols/rendezvous/src/codec.rs` | Proto2 message types, status codes, signed PeerRecord and cookie format | `rendezvous::codec` |

## Accepted Rules

- DHT and rendezvous live in `fcl_p2p`, not `fcl::plugins::p2p::node`.
- Public API stays owner-shaped: `dht::options`, `dht::query_result`,
  `rendezvous::options`, `rendezvous::registration`, `discovery::policy`.
- RocksDB remains a replaceable `peer_store` backend detail and does not appear
  in public modules.
- DHT/rendezvous messages are full length-delimited libp2p protocol payloads;
  payload-only helpers are not public API.
- Live support claims require matching artifacts from `test_fcl_libp2p_interop`.

## Current Proof

| Case | Status | Proof |
|---|---|---|
| DHT protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| DHT codec and malformed rejection | Ported | `p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed` |
| DHT XOR distance and bounded routing result | Ported | `p2p_dht_routing_table_uses_sha256_xor_distance_and_bounds_results` |
| DHT node handler over negotiated stream | Ported | `p2p_dht_node_finds_peer_and_provider_over_negotiated_stream` |
| DHT iterative many-peer lookup | Ported | `p2p_dht_iterative_lookup_walks_many_peer_topology` |
| DHT iterative provider lookup and provide | Ported | `p2p_dht_iterative_provider_lookup_and_provide_reach_closest_peers` |
| DHT durable routing/provider state | Ported | `p2p_peer_store_rocksdb_persists_discovery_dht_and_rendezvous_state` |
| DHT live peer lookup | Ported | `test_fcl_libp2p_interop`: `dht_find_peer` FCL ↔ go-libp2p/rust-libp2p |
| DHT live provider lookup | Ported | `test_fcl_libp2p_interop`: `dht_provide_find_provider` FCL ↔ go-libp2p/rust-libp2p |
| Rendezvous protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| Rendezvous codec, TTL, cookie and status | Ported | `p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status` |
| Rendezvous node handler over negotiated stream | Ported | `p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream` |
| Rendezvous refresh, replacement and cookie continuation | Ported | `p2p_rendezvous_refresh_replaces_registration_and_cookie_discovers_new_records` |
| Rendezvous durable registration state | Ported | `p2p_peer_store_rocksdb_persists_discovery_dht_and_rendezvous_state` |
| Rendezvous live register/discover | Ported | `test_fcl_libp2p_interop`: `rendezvous_register_discover` FCL ↔ rust-libp2p |
| Discovery refresh feeds AutoRelay | Ported | `p2p_discovery_refresh_learns_dht_and_rendezvous_relay_candidates_for_autorelay` |

## Unsupported Gaps

- Live donor fixtures for repeated many-peer DHT/rendezvous refresh topologies
  are still limited. FCL component simulations cover the lifecycle; live matrix
  artifacts remain peer/provider lookup and Rust rendezvous register/discover.
- Automatic long-running local provider republish worker is not a separate
  public support claim. Explicit `async_provide(...)` refreshes the provider
  record and F.2 validates provider TTL pruning and publication over negotiated
  DHT streams.
- Go Rendezvous behaviour proof is not claimed because no official go-libp2p
  rendezvous behaviour donor is present in the workspace.

These gaps are also tracked in `tests/libp2p_interop/donor_cases.json`.
They must not be described as supported until matching donor-derived tests and
live artifacts are produced.

GossipSub/pubsub is tracked separately in
`docs/donors/fcl-p2p-gossipsub-v1.md` and is no longer a DHT/Rendezvous gap.
