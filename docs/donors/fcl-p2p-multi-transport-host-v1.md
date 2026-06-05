# FCL P2P Multi-Transport Host Traceability

This note is proof traceability for the E.3 host-level multi-transport
orchestration work. It is not a second roadmap; the canonical block order
remains `docs/network/quic-p2p.md`.

## Scope

Supported in this slice:

- multiple direct listeners on one node, currently QUIC and TCP;
- `node::local_endpoints()` as the full advertised endpoint set;
- `node::local_endpoint()` as first-endpoint compatibility convenience;
- Identify and peer exchange advertisement of the canonical host address set;
- peer-store learning of multiple direct endpoints without duplicates;
- path selection over supported direct QUIC/TCP endpoint candidates with
  per-endpoint backoff and score filtering;
- relay remaining above the direct transport boundary.

Out of scope:

- browser/proxy `/ws` and `/wss` dial/listen paths;
- a new public multi-transport library;
- Relay ownership inside `fcl::p2p::direct`;
- product/plugin/API orchestration above `fcl_p2p`.

## Donors Inspected

| Area | Donor files | Accepted pattern |
| --- | --- | --- |
| Default transport/listen shape | `donors/go-libp2p/defaults.go`, `donors/go-libp2p/libp2p_test.go` | A host can register and listen on multiple transports; missing transport support is a hard listen/dial failure. |
| Transport composition | `donors/rust-libp2p/libp2p/src/builder.rs` | The host/swarm composes TCP, QUIC, DNS and optional relay transports above concrete transport implementations. |
| Relay composition | `donors/rust-libp2p/libp2p/src/builder/phase/relay.rs` | Relay is composed as a relay path over authenticated multiplexed transport, not as a direct transport profile. |
| Address semantics | `donors/libp2p-specs/addressing/README.md` | Exchanged peer addresses carry network address plus peer identity; relayed addresses use `/p2p-circuit` semantics. |
| Identify address hygiene | `donors/go-libp2p/p2p/protocol/identify/id.go`, `donors/libp2p-specs/identify/README.md` | Learned listen addresses are filtered using authenticated connection context; observed addresses stay separate from listen addresses. |
| Routing-record routability | `donors/libp2p-specs/RFC/0003-routing-records.md` | Third-party distributed peer addresses should be public-routable unless an explicit local/private context is known. |
| Dial ranking groups | `donors/go-libp2p/p2p/net/swarm/dial_ranker.go` | Direct public/private and relay candidates are classified before dialing; relay is delayed/selected above raw direct transport execution. |
| Observed address ownership | `donors/go-libp2p/p2p/host/observedaddrs/manager.go` | Observed addresses are host reachability input, not blindly learned peer listen records. |

## FCL Coverage

| Behavior | FCL component test | Current claim |
| --- | --- | --- |
| QUIC and TCP can listen at the same time | `test_fcl_quic_p2p p2p_node_listens_on_quic_and_tcp_and_identify_advertises_both` | Supported for direct QUIC and direct TCP. |
| First endpoint compatibility remains | Same test checks `local_endpoint()` still returns a value | `local_endpoint()` remains convenience only; new consumers should prefer `local_endpoints()`. |
| Duplicate direct listen is rejected typed | `test_fcl_quic_p2p p2p_duplicate_direct_listen_rejects_typed` | Supported. |
| Identify advertises every active direct endpoint with `/p2p/<local-peer>` | `test_fcl_quic_p2p p2p_node_listens_on_quic_and_tcp_and_identify_advertises_both` | Supported for active direct listeners and configured advertised endpoints. |
| Peer exchange preserves multiple endpoints without duplicates | `test_fcl_quic_p2p p2p_peer_exchange_preserves_multiple_direct_endpoints_without_duplicates` | Supported; peer exchange now uses negotiated protocol streams. |
| Mismatched learned endpoint peer suffix is rejected | `test_fcl_quic_p2p p2p_identify_push_rejects_mismatched_endpoint_peer_suffix` | Supported for Identify and peer exchange learning. |
| Equivalent advertised endpoints are canonicalized and deduplicated | `test_fcl_quic_p2p p2p_local_endpoints_collapse_canonical_equivalent_advertised_endpoints` | Supported for configured advertised endpoints after local peer suffix normalization. |
| Third-party peer exchange filters non-routable direct endpoints | `test_fcl_quic_p2p p2p_peer_exchange_filters_non_routable_third_party_endpoints` | Public IP, DNS and relay candidates are learnable from third parties; loopback, link-local, private and localhost DNS are rejected. |
| Authenticated loopback connections may learn loopback listen addresses | Existing local Identify/multi-listen component coverage uses loopback connections and keeps local advertised endpoints learnable | Supported for local component/private-network scenarios; private addresses are not globally banned. |
| Stop closes all direct listeners | `test_fcl_quic_p2p p2p_stop_closes_all_direct_listeners` | Supported for active QUIC/TCP direct listeners. |
| `/ws` and `/wss` are parse/store only | `test_fcl_quic_p2p p2p_websocket_multiaddr_is_parseable_but_not_dialable` | No dial/listen path is claimed. |
| Path selector skips backed-off endpoints before falling back | Existing timeout/backoff path-manager tests plus `path_selector::rank_direct` coverage through direct open paths | Supported for direct QUIC/TCP endpoint records; relay remains separate. |

## Notes

- `fcl::p2p::direct` remains direct-only: QUIC/TCP profiles, direct listeners
  and direct connect/accept. It does not own Identify, DHT, Relay, peer exchange
  or address advertisement policy.
- Host-level helpers own address hygiene and path ordering inside `fcl_p2p`.
- Peer exchange is an FCL protocol and is now selected with `multistream-select`
  like other negotiated P2P protocol streams.
