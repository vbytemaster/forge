# Quarantined ObjectDB Prototype

This directory contains the first `forge_objectdb` prototype exactly as an
archival reference. It is intentionally outside the active `libraries/` and
`tests/` trees.

The prototype is not public API:

- it is not built;
- it is not installed;
- it is not exported as a Forge package component;
- its tests are not part of the active test suite.

The clean object database work starts from the active docs instead:

- `docs/iterations/forge-object-database-v1.md`;
- `docs/donors/forge-objectdb-donor-baseline-v1.md`;
- `libraries/ids` for canonical `{space,type,instance}` object identity.

Future `forge::objectdb` work should use the planned `store`, `catalog`,
key/index/cursor and storage-neutral primitive direction. Do not treat this
quarantined prototype as the target public API shape.
