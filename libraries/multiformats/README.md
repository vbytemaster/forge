# forge_multiformats

`forge_multiformats` owns the libp2p-compatible multiformat primitives used by
`forge_p2p`: unsigned varint, multicodec constants, multihash, multibase and
multiaddr parsing/encoding. It is a compatibility substrate, not a P2P node and
not an application addressing policy layer.

## When To Use

- A library needs to parse or emit libp2p-style multiaddrs.
- P2P code needs multibase/multihash/multicodec values without depending on
  Go/Rust libp2p runtimes.
- Tests need donor-compatible binary/text vectors for address and hash formats.

## When Not To Use

- Do not put peer-store, routing, relay, DHT or rendezvous policy here.
- Do not treat a syntactically valid multiaddr as dialable or trusted. Host
  address policy belongs to `forge_p2p`.
- Do not add application endpoint names, storage locations or authorization rules.

## Public Modules

- `forge.multiformats.varint`
- `forge.multiformats.multicodec`
- `forge.multiformats.multihash`
- `forge.multiformats.multibase`
- `forge.multiformats.multiaddr`
- `forge.multiformats.types`
- `forge.multiformats.exceptions`
- `forge.multiformats`

Target: `forge_multiformats`.

Dependencies: `forge_exceptions`, `forge_crypto`.

## Examples

```cpp
import forge.multiformats.multiaddr;
import forge.multiformats.multihash;

auto address = forge::multiformats::multiaddr::parse(
   "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/12D3KooW...");

auto bytes = address.to_bytes();
auto roundtrip = forge::multiformats::multiaddr::from_bytes(bytes).to_string();

std::vector<std::uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
auto hash = forge::multiformats::multihash::sha2_256(payload);
auto encoded = hash.encode();
```

## Boundaries

- Multiaddr parsing accepts the supported libp2p address vocabulary, including
  relay/circuit forms, but it does not decide routability.
- WebSocket multiaddrs can be parsed as data, but WebSocket transport support is
  owned by concrete transport/P2P integration work.
- Donor compatibility is proven through vectors and interop tests, not by
  linking libp2p source.

## Tests

`test_forge_multiformats` covers varint minimal encoding, multicodec constants,
multihash roundtrips, multibase prefixes, multiaddr binary/text donor vectors,
encapsulation/decapsulation and malformed input typed errors.
