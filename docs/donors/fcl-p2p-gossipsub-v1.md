# FCL P2P GossipSub v1 Donor Traceability

## Scope

Этот проход закрывает `fcl_p2p` PubSub через libp2p GossipSub:

- `/meshsub/1.1.0` — основной поддержанный протокол.
- `/meshsub/1.0.0` — fallback для совместимости.
- `/meshsub/1.2.0`, `/meshsub/1.3.0`, partial messages и FloodSub не входят в текущий support claim.

Алгоритмы mesh/scoring/heartbeat/publish живут в `fcl_p2p`; application layer только конфигурирует и потребляет их.

## Donors

- `donors/libp2p-specs/pubsub/gossipsub/gossipsub-v1.1.md`
- `donors/go-libp2p-pubsub/pb/rpc.proto`
- `donors/go-libp2p-pubsub/sign.go`
- `donors/go-libp2p-pubsub/sign_test.go`
- `donors/go-libp2p-pubsub/gossipsub.go`
- `donors/go-libp2p-pubsub/gossipsub_test.go`
- `donors/go-libp2p-pubsub/validation_test.go`
- `donors/go-libp2p-pubsub/gossipsub_spam_test.go`
- `donors/go-libp2p-pubsub/rpc_queue_test.go`
- `donors/go-libp2p-pubsub/score_test.go`
- `donors/go-libp2p-pubsub/backoff_test.go`
- `donors/go-libp2p-pubsub/mcache_test.go`
- `donors/rust-libp2p/protocols/gossipsub/src/protocol.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/config.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/types.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/mcache.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/queue.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/backoff.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/time_cache.rs`
- `donors/rust-libp2p/protocols/gossipsub/src/peer_score/tests.rs`
- `donors/rust-libp2p/protocols/gossipsub/tests/smoke.rs`

## Ported Cases

| Behavior | FCL coverage | Live proof |
|---|---|---|
| Protocol ids `/meshsub/1.1.0` and `/meshsub/1.0.0` | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` | `test_fcl_libp2p_interop gossipsub_publish` |
| RPC subscriptions, publish and control messages | `p2p_gossipsub_codec_roundtrips_v11_rpc_and_rejects_malformed` | component/golden coverage |
| Message signing prefix and payload shape | `p2p_gossipsub_signing_rejects_tampered_payload` | FCL -> Go/Rust signed publish |
| FCL-to-FCL delivery over negotiated stream | `p2p_gossipsub_nodes_deliver_signed_publish_over_negotiated_stream` | component topology |
| FCL-to-FCL forwarding through mesh peer | `p2p_gossipsub_forwards_between_subscribed_peers` | component topology |
| FCL <-> Go/Rust publish delivery | `test_fcl_libp2p_interop` | FCL -> Go, Go -> FCL, FCL -> Rust, Rust -> FCL |
| 10-node FCL mesh stress and duplicate suppression | `p2p_gossipsub_ten_node_mesh_delivers_multiple_publishes_once` | component topology |
| Control spam and queue pressure limits | `p2p_gossipsub_control_spam_is_penalized_without_stopping_node`, `p2p_gossipsub_outbound_byte_limit_rejects_publish_without_stopping_node`, `p2p_gossipsub_validation_queue_limit_drops_excess_and_shutdown_is_clean` | component/resource coverage |
| Mixed FCL/Go/Rust mesh stress | `test_fcl_libp2p_interop gossipsub_mixed_mesh_stress` | 4 FCL + 3 Go + 3 Rust peers, three publishers, duplicate-free delivery matrix |

## Current Claim

FCL supports GossipSub v1.1 publish/subscribe wire compatibility with v1.0 fallback for the implemented API: subscribe, unsubscribe, publish, signed messages, duplicate cache, IHAVE/IWANT/GRAFT/PRUNE codec, heartbeat mesh maintenance, resource bounds and live delivery against Go/Rust fixtures.

This is not a durable queue. Delivery semantics are libp2p GossipSub semantics.

## Deferred

- FloodSub.
- GossipSub v1.2/v1.3 behavior.
- Partial messages extension.
- Product-level pubsub gateway.
