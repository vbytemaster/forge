# FCL P2P API Resolver Donor Traceability

## Scope

`fcl::plugins::p2p::resolver` is a plugin-level resolver for API-over-P2P
metadata. It composes through `fcl::plugins::p2p::node` and `fcl.transport.api`; it is not
`fcl_p2p` core discovery, product authorization, diagnostics or delivery.

The resolver network protocol `/fcl/api/resolver/1` is FCL-specific. libp2p
donors inform layering and acceptance criteria, but FCL does not claim Go/Rust
resolver protocol interoperability.

## Donors

- libp2p Identify: peers advertise supported protocol ids, not application API
  descriptors. FCL keeps Identify in `fcl_p2p` and puts API metadata above it.
- go-libp2p host/protocol registration: product protocols are mounted as named
  protocol handlers above the host.
- rust-libp2p behaviour composition: discovery and behaviour metadata are
  composed around a shared swarm/host instead of every service owning transport.
- Kubo `CoreAPI`: consumers use narrow service facades over a running node.
- Boxo service decomposition: focused services sit above the host; one plugin
  must not become a content, routing, diagnostics and delivery superplugin.

## Accepted Patterns

- Resolver publishes actual product API routes through `fcl::plugins::p2p::node`.
- Resolver metadata is a stable serializable projection: API id/version,
  protocol id string, codec, limits, methods and error identities.
- Raw `fcl::api::descriptor` is local runtime metadata and is not sent over the
  wire because it contains function/type state.
- Resolver cache is bounded by peer count and TTL.
- Malformed, duplicate or over-limit remote metadata is rejected and not cached.
- Peer identity comes from the authenticated P2P stream; resolver payloads do
  not claim or override peer id.

## Rejected Patterns

- No second frame loop: the resolver protocol is served through
  `fcl.transport.api`.
- No direct access to `fcl::p2p::node` internals from product plugins.
- No product authorization, business routing policy, durable delivery or
  diagnostics inside the resolver.
- No Go/Rust live support claim for `/fcl/api/resolver/1`.

## FCL Tests

- `test_fcl_plugins p2p_api_resolver_plugin_config_is_described_from_public_schema`
- `test_fcl_plugins p2p_api_resolver_publishes_metadata_and_delegates_route_mounting`
- `test_fcl_plugins p2p_api_resolver_rejects_duplicate_api_and_resolver_protocol_conflict`
- `test_fcl_plugins p2p_api_resolver_resolves_remote_api_and_opens_typed_remote`
- `test_fcl_plugins p2p_api_resolver_enforces_version_compatibility`
- `test_fcl_plugins p2p_api_resolver_rejects_malformed_remote_metadata_without_caching_it`
- `test_fcl_plugins p2p_api_resolver_cache_ttl_and_force_refresh_are_behavioral`

## Support Claim

Supported: FCL-to-FCL API metadata resolution over authenticated P2P streams.

Known gap: external Go/Rust peers do not implement the FCL resolver protocol;
Go/Rust libp2p interop remains covered by the underlying P2P transports and
libp2p protocols, not by this FCL-specific metadata service.
