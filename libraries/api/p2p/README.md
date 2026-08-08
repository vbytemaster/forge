# Forge API P2P

`forge_api_p2p` is the typed API binding adapter for negotiated P2P protocol streams.

It owns only API frame binding behavior:

- public modules live under `forge.api.p2p.*`;
- public namespace is `forge::api::p2p`;
- P2P identity, discovery, sessions and protocol streams stay in `forge_net_p2p` / `forge::net::p2p`;
- generic stream frame serving stays in `forge_api_stream`.

The default application protocol is `/forge/api/2`. Every selected stream,
including a product-owned custom protocol id, performs the mandatory symmetric
Forge API wire-v2 hello before accepting calls. This does not change libp2p
security, multiplexing, peer identity or protocol negotiation.

Use this library when an API contract should be published through a P2P node.
