# FCL P2P Reachability And Relay v1 Donor Matrix

## Purpose

This pass moves reachability, relay and hole punching out of the old
FCL-only control path and into libp2p-compatible protocol lanes selected by
`multistream-select`. The compatibility bar is donor-derived: specs and
go-libp2p/rust-libp2p tests define the behavior we can claim as supported.

## Donor Sources Inspected

- `donors/libp2p-specs/autonat/autonat-v1.md`
- `donors/libp2p-specs/autonat/autonat-v2.md`
- `donors/libp2p-specs/relay/circuit-v2.md`
- `donors/libp2p-specs/relay/DCUtR.md`
- `donors/libp2p-specs/connections/hole-punching.md`
- `donors/go-libp2p/libp2p_test.go`
- `donors/go-libp2p/proto_test.go`
- `donors/rust-libp2p/libp2p/src/builder/phase/relay.rs`
- `donors/rust-libp2p/libp2p/src/builder/phase/quic.rs`

## Traceability Matrix

| Area | Spec Source | Donor Cases Inspected | FCL Coverage | Current Claim |
| --- | --- | --- | --- | --- |
| AutoNAT v1 | `autonat-v1.md` | go-libp2p AutoNAT service smoke and protobuf registration tests | Protocol id, deterministic protobuf codec, inbound dial request handling, outbound probe through `/libp2p/autonat/1.0.0` | Supported subset: v1 dial probe over known peer |
| AutoNAT v2 | `autonat-v2.md`, amplification attack diagram | go-libp2p AutoNAT v2 service smoke and rust-libp2p AutoNAT v2 server behavior | Protocol ids, DialRequest/DialResponse, DialDataRequest/Response, DialBack/DialBackResponse, nonce validation, data-size limits, FCL-to-FCL public probe, durable observation test and live Go/Rust interop harness | Live Ping/Identify/AutoNAT v2 proof is wired through `test_fcl_libp2p_interop` when enabled |
| Circuit Relay v2 Hop | `circuit-v2.md`, RFC 0002 signed envelopes | go-libp2p relay setup/dial tests, voucher tests, rust relay builder path | `/libp2p/circuit/relay/0.2.0/hop`, RESERVE refresh, signed reservation voucher, CONNECT/status codec, reservation TTL/limit response, target-owned reservation, p2p-circuit address parsing, reservation-over-relay rejection, relay byte pumping | Component-supported for one relay hop; live reserve proof is wired for FCL<->Go and FCL<->Rust when enabled |
| Circuit Relay v2 Stop | `circuit-v2.md` | go-libp2p relay dialing behavior | `/libp2p/circuit/relay/0.2.0/stop`, connect/status codec, relayed stream protocol negotiation, arbitrary registered protocol over the relayed stream, byte pass-through, byte-limit enforcement | Component-supported for one relay hop |
| DCUtR | `DCUtR.md`, `hole-punching.md` | go-libp2p AutoRelay/circuit tests, rust relay transport path | `/libp2p/dcutr`, CONNECT/SYNC codec, RTT-based attempt state, duplicate/in-flight protection, bounded retry state, FCL three-node relay topology artifact and mixed FCL source -> go-libp2p relay -> go-libp2p destination scenario | Supported for one relay hop at the protocol-wire level; live Rust relay topology remains the next proof gate before claiming full Rust parity |
| Path Manager | libp2p connection and relay behavior | go-libp2p direct/relay examples and resource-manager wrapping test | `path::policy/result`, direct first, hole punch when relay candidate exists, relay fallback, peer-store scoring/backoff reuse | Supported FCL behavior; global relay discovery remains future DHT/rendezvous work |
| Resource Manager | go-libp2p resource-manager wrapping test | go-libp2p circuit dial with wrapped resource manager | `resource_manager::limits/snapshot`, peer/protocol stream scopes, relay reservation scopes, dial budget, malformed-message budget, relay stream/byte limits, relay/path policies in node path | Covered by local scopes; live harness is the required proof gate when enabled |

## Accepted Patterns

- Built-in reachability/relay/hole-punch protocols must be selected through
  `multistream-select`, not through `/fcl/p2p/control/1`.
- Relay is a bounded service: reservation, TTL, stream and byte limits are part
  of the protocol path, not optional diagnostics.
- DCUtR is coordinated over a relay path and must return a typed status instead
  of hiding failure as an opaque transport exception.
- Peer/path state remains in `fcl_p2p`; plugin layers must not add their own
  AutoNAT, AutoRelay, relay or hole-punch loops.

## Unsupported Gaps Kept Honest

- Live go-libp2p/rust-libp2p fixtures are wired as a strict CTest harness for
  Ping, Identify, AutoNAT v2 and Relay reservation. They are disabled by default
  for ordinary builds and become hard failures when
  `FCL_ENABLE_LIBP2P_INTEROP=ON`.
- DCUtR now has two topology artifacts in the interop runner: FCL-only
  relay/source/destination and mixed FCL source -> go-libp2p relay ->
  go-libp2p destination. The mixed scenario proves the donor relay service,
  reservation and DCUtR wire path; live Rust topology remains the next
  compatibility proof gate.
- Signed relay vouchers use RFC 0002 signed envelopes with the
  `libp2p-relay-rsvp` domain, typed payload, signer Peer ID verification and
  stale/invalid signature rejection.
- Global AutoRelay discovery is not implemented; relay candidates come from
  peer store records, Identify capabilities and on-demand reservation attempts.
- Multi-hop relay routing is out of scope.
