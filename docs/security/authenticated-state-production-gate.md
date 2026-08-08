# Authenticated state production activation gate

`forge.db.authenticated` and the ranked proof verifier are Experimental. A
consumer must not activate their state root in a production genesis until an
independent cryptographic and implementation review has accepted all of the
following as one versioned contract:

- SHA-256 domain separation and length framing for values, leaves and inner
  nodes;
- ordered AVL+ balancing, interval metadata, subtree sizes and root binding;
- membership, non-membership, ranked range and change/tombstone proofs;
- canonical binary decoding, parser bounds, overflow handling and hostile
  allocation limits;
- pruning, garbage collection, retained-root policy and proof validity after
  server pruning;
- finality binding performed by the product-specific verifier above the neutral
  Forge interface.

The review must publish the exact Forge commit, proof schema and persisted
format versions, threat model, findings and remediation status. Unit tests,
fuzzing, donor differential tests and process-crash acceptance are prerequisites,
not substitutes for this independent review.

Until that artifact exists, release packages may expose the Experimental API
for integration and test networks, but production configuration must keep
authenticated-state activation disabled.
