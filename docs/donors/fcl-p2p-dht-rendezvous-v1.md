# FCL P2P DHT/Rendezvous Discovery v1 Donor Note

## Scope

This note tracks the first production-shaped discovery slice in `fcl_p2p`.
The implementation adds owner modules for Kademlia-compatible DHT and
libp2p rendezvous, durable state through `peer_store`, and node-level
protocol handlers selected through multistream-select.

Supported claims are intentionally narrow until live Go/Rust artifacts exist.
The current slice has component proof for wire codecs, FCL-to-FCL negotiated
streams, routing/provider state and rendezvous register/discover state. Full
libp2p DHT/rendezvous support still requires live donor interop for peer
lookup, provider lookup and register/discover.

## Donor Sources

| Area | Donor source | Accepted pattern | FCL target |
|---|---|---|---|
| Kademlia DHT | `donors/libp2p-specs/kad-dht/README.md` | XOR distance over `sha256(key)`, `k=20`, `alpha=10`, bounded query timeouts | `fcl.p2p.dht`, `dht::routing_table`, `node::async_find_peer` |
| DHT wire messages | `donors/rust-libp2p/protocols/kad/src/generated/dht.proto` | Length-delimited protobuf message with `FIND_NODE`, `ADD_PROVIDER`, `GET_PROVIDERS` | `dht::codec`, `node::impl::handle_dht` |
| Rendezvous protocol | `donors/libp2p-specs/rendezvous/README.md` | `/rendezvous/1.0.0`, register/discover/unregister, TTL, namespace limits, cookie continuation | `fcl.p2p.rendezvous`, `node::impl::handle_rendezvous` |
| Rendezvous wire messages | `donors/rust-libp2p/protocols/rendezvous/src/generated/rpc.proto` | Proto2 message types and status codes | `rendezvous::codec` |

## Accepted Rules

- DHT and rendezvous live in `fcl_p2p`, not `fcl::plugins::p2p_node`.
- Public API stays owner-shaped: `dht::options`, `dht::query_result`,
  `rendezvous::options`, `rendezvous::registration`, `discovery::policy`.
- RocksDB remains a replaceable `peer_store` backend detail and does not appear
  in public modules.
- DHT/rendezvous messages are full length-delimited libp2p protocol payloads;
  payload-only helpers are not public API.
- Unsupported live interop is explicit and does not become a product claim.

## Current Proof

| Case | Status | Proof |
|---|---|---|
| DHT protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| DHT codec and malformed rejection | Ported | `p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed` |
| DHT XOR distance and bounded routing result | Ported | `p2p_dht_routing_table_uses_sha256_xor_distance_and_bounds_results` |
| DHT node handler over negotiated stream | Ported | `p2p_dht_node_finds_peer_and_provider_over_negotiated_stream` |
| DHT durable routing/provider state | Ported | `p2p_peer_store_rocksdb_persists_discovery_dht_and_rendezvous_state` |
| Rendezvous protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| Rendezvous codec, TTL, cookie and status | Ported | `p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status` |
| Rendezvous node handler over negotiated stream | Ported | `p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream` |
| Rendezvous durable registration state | Ported | `p2p_peer_store_rocksdb_persists_discovery_dht_and_rendezvous_state` |

## Unsupported Gaps

- Live FCL ↔ go-libp2p and FCL ↔ rust-libp2p DHT peer/provider lookup.
- Live FCL ↔ go-libp2p and FCL ↔ rust-libp2p rendezvous register/discover.
- Full iterative Kademlia query scheduling across many peers with donor live
  topology artifacts.
- Global AutoRelay discovery policy over DHT/rendezvous.
- GossipSub/pubsub.

These gaps are also tracked in `tests/libp2p_interop/donor_cases.json`.
They must not be described as supported until the matching live artifacts are
produced by `test_fcl_libp2p_interop`.
