# Forge P2P Production Hardening v1

> **Status:** accepted priority direction, implementation pending.
>
> This document records the production integration gaps found while preparing
> Forge Content Swarm. It does not change the public API by itself.

## 1. Audit Standard

A libp2p feature is not considered delivered merely because its codec, protocol
handler, low-level `forge_net_p2p` method or interoperability test exists. A
production claim additionally requires:

- configuration through both `forge_net_p2p` options and the official
  `plugins.p2p.node` adapter;
- autonomous activation and deterministic shutdown through the node lifecycle,
  with the plugin delegating to that lifecycle;
- bounded scheduling, cancellation, retry and backoff;
- diagnostics that expose the effective state;
- focused raw-node tests and an integration test that starts official plugins
  over the same node behavior.

The low-level implementations remain valuable and should be reused. The gap is
primarily incomplete node autonomy and official-plugin wiring, not a request to
introduce a second P2P stack.

## 2. Current Reliable Baseline

The following paths are connected through the official plugin today, subject to
the production-startup gap below:

- direct QUIC and TCP/Yamux sessions to configured or already known peers;
- bounded bootstrap reconnect and bootstrap-session protection;
- inbound application protocol routing and Forge API over a known `peer_id`;
- session, stream and resource admission limits;
- diagnostics over the shared node;
- GossipSub publish/subscribe over peers that are already connected.

This is a static/bootstrap-centric topology. It is not yet an autonomous
libp2p mesh.

## 3. Node Autonomy Principle

`forge::net::p2p::node` is the reusable libp2p host, not a passive collection of
protocol methods that requires a Forge App plugin to remain healthy. Once
constructed with explicit policy and started, it owns all transport-neutral
network mechanics and their maintenance:

- bootstrap connection maintenance;
- Identify and Identify Push;
- DHT routing refresh and provider-record maintenance;
- Rendezvous registration/discovery refresh;
- Peer Exchange scheduling;
- AutoNAT probing and reachability state;
- AutoRelay candidate selection and reservation renewal;
- DCUtR attempts and relay fallback;
- connection watermarks, scoring, liveness sampling and GossipSub heartbeat.

`plugins.p2p.node` is an application adapter. It decodes configuration, prepares
or injects durable dependencies, constructs the node, delegates start/stop and
exports narrow local APIs. It must not implement parallel protocol loops,
inspect diagnostics as control state or become required for a programmatic
`forge_net_p2p` deployment.

Explicit application intent remains outside autonomous maintenance. For
example, Content Swarm decides which content key to provide; after
`async_provide(key)`, the node owns DHT publication, TTL refresh and routing
mechanics for the caller-controlled registration lifetime. The caller must be
able to withdraw or release that registration when content is no longer
available. Products continue to own authorization and network membership.

## 4. Priority Findings

### P0: Production Node Startup And Persistence

`forge_net_p2p` correctly requires a persistent peer store outside insecure
test mode. The official plugin neither exposes a peer-store path/backend in its
configuration nor supplies one when constructing the node. Consequently, the
configured plugin cannot currently satisfy the production node contract; its
integration tests use insecure test mode.

Required outcome:

- expose a product-neutral persistent peer-store configuration;
- let the node own peer-store use and close it through its lifecycle, while the
  plugin maps configured storage or injects a backend;
- reject invalid or unwritable storage before network admission opens;
- retain learned peers, endpoint observations, DHT/provider records,
  Rendezvous registrations, relay reservations and backoff state across restart;
- add a secure official-plugin startup/reopen test without
  `allow-insecure-test-mode`.

### P1: Production Resource Policy

The low-level node has bounded defaults for sessions, streams, protocols,
pending dials, malformed messages, relay traffic, transport queues and discovery
messages. The official plugin currently exposes only a small subset of those
limits, so an operator cannot tune the shared node for the deployment workload
or reduce limits for an exposed edge node.

Required outcome:

- expose structured, product-neutral configuration for session totals and
  directions, sessions per peer and pending session admission;
- expose stream totals, per-peer/per-protocol limits, dial budgets, malformed
  message budgets and relay byte/queue limits;
- expose transport queue/buffer limits and Peer Exchange message/record limits;
- expose discovery concurrency, result, timeout and refresh limits without
  duplicating protocol-specific configuration records;
- expose API stream item, buffered-byte, idle, shutdown and inflight limits;
- validate cross-field invariants before constructing the node;
- report the effective limits in diagnostics so deployment configuration can be
  audited;
- keep conservative bounded defaults; configuration is not permission to make
  queues or admission unbounded.

Not every private implementation constant needs a public setting. A value must
be configurable when it changes deployment capacity, exposure or failure
behavior.

### P1: Bootstrap Startup And Maintenance

The plugin currently implements bootstrap maintenance itself and attempts every
configured endpoint sequentially inside `startup()`. Each failed endpoint can
consume its full connect timeout, so application startup latency grows linearly
with the bootstrap list. The maintenance loop then scans a full diagnostics
snapshot once per bootstrap peer and uses deterministic retry delays without
jitter. This logic belongs in the node because programmatic users require the
same behavior.

Required outcome:

- make disconnected startup versus required initial connectivity an explicit
  node policy supplied by the deployment;
- bound the complete initial-bootstrap phase by one startup budget;
- attempt bootstrap peers with bounded parallelism rather than sequentially;
- move non-required retry work to a node-owned managed maintenance task;
- add randomized jitter to exponential backoff to prevent synchronized retry
  storms after a shared bootstrap failure;
- use an allocation-free internal session lookup rather than constructing
  diagnostics snapshots in the control loop;
- protect successful bootstrap sessions as today and release protection when a
  configured bootstrap entry is removed;
- cancel in-progress DNS/connect waits and maintenance timers deterministically
  during shutdown.

Diagnostics snapshots remain an operator-facing projection. They must not be
used as an internal point-query API.

### P1: Identity Material Ownership

The plugin accepts certificate and private-key PEM as complete config string
values. Schema redaction prevents ordinary diagnostics from printing the key,
but there is no node-owned file, encrypted-file or secret-provider source
contract. A stable PEM supplied by the application still produces a stable
identity; the gap is secure operational delivery, not identity derivation.

Required outcome:

- support a stable identity source suitable for mounted secrets and encrypted
  local configuration;
- reuse Forge secret-source/loading mechanics or extract a neutral reusable
  component instead of adding a P2P-only file parser;
- load and validate identity material before opening listeners;
- avoid copying private material into diagnostics, generated examples or error
  context;
- define reload behavior explicitly; v1 may require restart rather than rotate
  a live libp2p identity;
- preserve programmatic construction for callers that already own a secure
  identity provider.

### P1: Identify And Remote Capability Truth

Forge responds to Identify and accepts Identify Push, but an ordinary bootstrap
or direct connection does not initiate Identify. Identify Push is not emitted
when the local address/protocol set changes. New session records are initially
populated from local capabilities rather than verified remote capabilities.

Required outcome:

- run bounded Identify after each newly established session;
- treat TLS/Noise peer authentication and Identify protocol advertisement as
  separate facts;
- populate session and peer-store capabilities only from the remote document;
- emit Identify Push after a relevant local endpoint or protocol change;
- reject or quarantine inconsistent peer/address/capability observations;
- prove Forge-to-Forge capability learning through official plugins.

### P1: Discovery Lifecycle And Topology Maintenance

Kademlia DHT, provider records and Rendezvous are implemented in
`forge_net_p2p`, but the node has no autonomous discovery lifecycle and the
official plugin neither configures one nor calls `async_refresh_discovery()`.
Discovery results alone are also insufficient: the node must maintain a bounded
set of useful sessions.

Required outcome:

- add explicit node options for DHT client/server and Rendezvous
  client/server roles, mapped by the plugin;
- let the node start an initial discovery pass after bootstrap connectivity;
- run one node-owned cancellable refresh loop with TTL-aware refresh, bounded
  parallelism, retry and jittered backoff;
- maintain configurable low/target/high peer watermarks;
- select and dial scored discovered peers without displacing protected
  bootstrap sessions;
- expire stale observations and disconnect obsolete unprotected sessions;
- stop discovery and dialing deterministically before node teardown.

Products may disable decentralized discovery for a deliberately static
deployment, but the mode must be explicit and diagnostics must report it.

### P1: Peer Exchange Activation

The Peer Exchange protocol can answer inbound requests and its capability is
advertised, but the node never schedules an outbound exchange. Therefore it
does not currently expand either a programmatic or official-plugin topology.

Required outcome:

- let the node request Peer Exchange from a bounded subset of identified
  compatible peers;
- trigger it during initial topology formation and at a bounded refresh rate;
- retain existing endpoint sanitization, record limits and per-peer backoff;
- feed accepted records into the same topology manager as DHT and Rendezvous;
- prove that a node learns and connects to a non-bootstrap peer.

### P1: Reachability, AutoRelay And Hole Punching

AutoNAT handlers and manual probing exist, while AutoRelay maintenance is
started automatically. The node does not autonomously schedule reachability
probes, and the plugin cannot supply their policy. AutoRelay is normally starved
of candidates because Identify, DHT, Rendezvous and Peer Exchange do not
populate the production peer set. Circuit Relay v2 and DCUtR hole punching
therefore work mainly in focused raw-node/interoperability tests.

Required outcome:

- configure trusted AutoNAT observers and a bounded re-probe policy through
  node options;
- aggregate observations instead of trusting one peer;
- publish effective public/private/unknown reachability in diagnostics;
- feed verified relay-capable peers into the existing AutoRelay loop;
- renew and replace reservations before expiry;
- attempt DCUtR only with valid relay context and preserve relay fallback;
- prove direct, relayed and hole-punched official-plugin paths, including
  reservation expiry and peer loss.

The existing Relay, AutoRelay and DCUtR mechanics should be completed through
node-owned orchestration rather than duplicated in plugins.

### P2: Liveness And Operational Maintenance

Ping supports inbound responses and explicit RTT measurements, but the node has
no optional periodic health-sampling policy. Connection closure is observed,
yet diagnostics do not provide an actively maintained peer-health view.

Required outcome:

- define whether a deployment enables bounded periodic Ping sampling;
- avoid a mandatory ping storm for idle or very large networks;
- feed successful RTT and failures into existing scoring/backoff state;
- expose last successful contact, RTT and failure/backoff status;
- distinguish responder-only Ping support from active health monitoring in docs.

### P2: API Call And Peer Admission Policy

Forge API wire v2 has a negotiated 60-second idle timeout and real per-call
deadline timers. A zero total deadline intentionally permits long-lived streams;
it does not disable idle detection or cancellation. The node plugin nevertheless
exposes only a small part of the stream policy, defaults its total deadline to
zero and does not let a publishing plugin select the existing P2P
`require_known_peer` topology guard.

Required outcome:

- document total deadline, idle timeout and open deadline as separate concepts;
- let a published API choose bounded stream/session limits appropriate to that
  contract;
- allow an explicitly configured total deadline without forcing one onto
  legitimate long-lived streams;
- prove that abandoned and stalled calls release inflight capacity while other
  calls keep the multiplexed session active;
- expose topology admission such as `require_known_peer` only as an optional
  coarse guard;
- keep authenticated remote `peer_id` in request metadata for product-owned
  authorization and quotas;
- state explicitly that presence in peer store, transport reachability and
  successful mTLS/Noise identity verification do not by themselves authorize an
  application operation.

Product authorization remains in the published API implementation or its
binding policy. The node plugin must not turn peer discovery into an implicit
access-control list.

### P2: Maintenance Path Efficiency

Node-owned background maintenance must use direct internal state and bounded
work rather than operator diagnostics projections or repeated whole-store
materialization.

Required outcome:

- use direct internal point queries for active session, peer and reservation
  state needed by maintenance;
- ensure one maintenance tick is bounded by configured work limits rather than
  total peer-store size;
- avoid holding the node mutex while constructing large projections or awaiting
  network operations;
- instrument maintenance duration, attempted work, skipped work and backpressure;
- add scale regressions with many peers, sessions and bootstrap entries.

### P2: GossipSub Topology Completeness

The official PubSub plugin is operational over connected peers and its
heartbeat, validation and resource bounds are active. Its mesh cannot become
independent of configured bootstrap/static sessions until discovery and
topology maintenance are completed.

Required outcome:

- do not rewrite GossipSub;
- allow the shared topology manager to supply identified compatible peers;
- verify mesh repair after bootstrap loss and discovered-peer replacement;
- keep PubSub delivery explicitly non-durable and non-exactly-once.

## 5. Ownership Boundaries

`forge_net_p2p` owns protocol codecs, peer store, Identify, Ping, AutoNAT, DHT,
Rendezvous, Relay, DCUtR, GossipSub, scoring, resource-manager mechanics and the
autonomous maintenance loops that keep those mechanisms operational. Its node
lifecycle must be complete for both direct library consumers and plugins.

`plugins.p2p.node` owns configuration mapping, dependency preparation, the
application-owned shared-node lifetime and narrow local APIs for other official
plugins. It calls the node lifecycle but does not reimplement bootstrap,
discovery, reachability, relay, liveness or gossip maintenance. It must not
expose an unrestricted raw-node escape hatch.

Product plugins own authorization, network/realm membership, application
protocols and business routing. `plugins.p2p.resolver` continues to open a typed
API on an already known peer; it does not become peer or content discovery.

## 6. Implementation Order

1. Make secure production startup possible with persistent peer-store and
   identity-source ownership.
2. Establish one explicit autonomous node lifecycle and move
   sequential/snapshot-based bootstrap maintenance into bounded node-owned
   orchestration.
3. Expose production resource policy through node options and plugin mapping.
4. Correct Identify and remote capability learning.
5. Add one node-owned discovery/topology lifecycle for DHT, Rendezvous and Peer
   Exchange.
6. Activate AutoNAT observations and prove AutoRelay/DCUtR through the official
   plugin path.
7. Add focused local discovery APIs needed by consumers, beginning with bounded
   DHT provider publication/lookup for Content Swarm.
8. Complete API admission/deadline policy, liveness diagnostics, maintenance
   scale gates and autonomous GossipSub mesh repair.

Each block requires focused raw-node tests and an official-plugin integration
test proving that the adapter selects the same behavior. Neither layer alone is
sufficient acceptance evidence.

## 7. Production Acceptance Gates

- A secure node starts with real identity material and persistent peer storage.
- Identity material can be supplied without embedding private PEM directly in a
  normal YAML document, and is absent from diagnostics and errors.
- Effective resource, transport, discovery and API stream limits match validated
  plugin configuration.
- Initial bootstrap work has bounded concurrency and one total startup budget;
  latency does not grow as the sum of every endpoint timeout.
- Simultaneous bootstrap loss does not produce synchronized retry storms.
- A programmatically constructed node performs the same configured bootstrap,
  discovery, reachability and relay maintenance without `plugins.p2p.node`.
- The official plugin contains no independent protocol maintenance loops and
  delegates lifecycle to that node behavior.
- Three nodes given only bootstrap entry points discover and connect to each
  other within bounded time.
- A restarted node restores valid peer/discovery state and safely expires stale
  records.
- DHT peer/provider lookup and Rendezvous discovery run through official plugin
  lifecycle and stop cleanly.
- Peer Exchange discovers a non-bootstrap node without accepting non-routable or
  identity-mismatched endpoints.
- Remote session capabilities match the peer's actual advertised protocols.
- Public and private reachability lead to the expected direct, relay and DCUtR
  paths.
- Loss of bootstrap, relay or a discovered peer repairs topology without an
  unbounded retry/task/memory increase.
- GossipSub continues delivery after bootstrap loss when other mesh peers remain.
- Long-lived API streams remain supported, while abandoned calls release
  inflight capacity under negotiated idle/deadline policy.
- Peer-store membership alone never grants product authorization.
- Bootstrap and discovery maintenance do not construct full diagnostics
  snapshots or perform unbounded work per tick.
- Diagnostics identify disabled, idle, degraded and healthy discovery states.
- Live Go/Rust libp2p interoperability remains green for every enabled protocol.

## 8. Non-Goals

- product authorization or chain/network membership;
- content-provider key design, seeding policy or transfer scheduling;
- replacing Forge API or `plugins.p2p.resolver`;
- a second peer database outside `forge_net_p2p`;
- unbounded background scanning, dialing, pinging or task creation.
