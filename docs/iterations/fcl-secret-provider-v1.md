# FCL Secret Provider v1

## Summary

Add a neutral FCL infrastructure plugin `secret_provider` for local secret
material and bounded cryptographic operations. It is the symmetric/data-secret
counterpart to `signature_provider`: products configure secret sources and
purpose policy once, while product plugins call a narrow local API by
`secret_id` and `purpose` instead of parsing environment variables, files or
inline secret fields themselves.

This block does not add Storlane, Spring, workspace, billing or storage
semantics to FCL. Downstream products decide which secret ids and purpose names
mean "workspace manifest decrypt", "chunk key derivation" or any other product
capability.

## Problem

FCL already has several pieces of the right foundation:

- schema-driven config and redaction;
- environment and `.env` source adapters;
- low-level crypto primitives such as AES-GCM, HKDF-SHA256 and scrypt;
- `signature_provider` as a local-only signer boundary with key ids, purposes
  and algorithm checks.

What is missing is the equivalent boundary for non-signing secret material.
Without it, downstream products tend to copy one of two bad patterns:

- every product plugin reads its own secret from config/env/file and becomes a
  mini secret store;
- one product-specific vault grows into a framework feature outside FCL and
  cannot be reused by other serious consumers.

`secret_provider` closes that gap without making FCL a product vault or a cloud
KMS wrapper.

## Donor-Derived Requirements

The detailed donor traceability is in
[`docs/donors/fcl-secret-provider-v1.md`](../donors/fcl-secret-provider-v1.md).

Accepted patterns:

- HashiCorp Vault Transit: operations are requested by key name and purpose;
  consumers do not receive long-lived root key material by default.
- Kubo/IPFS keystore: keys are addressed by stable refs and raw key output is
  not part of normal status/config flows.
- Kubernetes Secrets: environment variables and mounted files are valid secret
  delivery mechanisms for daemons.
- SOPS: encrypted config/files are useful for CI/CD and GitOps delivery.
- AWS KMS envelope encryption: data keys and wrapping keys are separate; local
  code should be able to unwrap/decrypt through an explicit boundary.
- FCL `signature_provider`: key id, purpose allow-list, config redaction and
  local-only API are the model for an official infrastructure plugin.

Rejected shortcuts:

- A product-specific `workspace_vault` or `storlane_vault` in FCL.
- A daemon-global magic singleton reachable outside `fcl_api`.
- Raw secret bytes in generated config, status, logs or diagnostics.
- Product plugins parsing secret-bearing environment variables directly.
- Reimplementing AES-GCM, HKDF or scrypt inside the plugin instead of using
  `fcl_crypto`.
- Treating Kubernetes Secret, GitLab CI variable or `.env` as a crypto boundary;
  they are only delivery sources.

## Public Shape

Target and package naming:

- target: `fcl_plugin_secret_provider`;
- namespace: `fcl::plugins::secret_provider`;
- public modules:
  - `fcl.plugins.secret_provider.types`;
  - `fcl.plugins.secret_provider.api`;
  - `fcl.plugins.secret_provider.exceptions`;
  - `fcl.plugins.secret_provider.plugin`.

The API is local-only and intentionally operation-oriented:

```cpp
status(query) -> snapshot;
get_bytes(get_request) -> get_result;
derive_hkdf_sha256(derive_request) -> derive_result;
encrypt_aes_gcm(aead_encrypt_request) -> aead_encrypt_result;
decrypt_aes_gcm(aead_decrypt_request) -> aead_decrypt_result;
```

Every request carries:

```cpp
std::string secret_id;
std::string purpose;
```

The provider validates that the secret exists, the purpose is allowed, the
operation is allowed for that secret, sizes are bounded, and the configured
algorithm/source can satisfy the request.

`get_bytes` exists only as a low-level escape hatch. It is denied unless the
specific secret sets `allow-raw-export=true`. For product root secrets, the
expected default is `allow-raw-export=false`, so consumers use decrypt/derive
operations instead of exporting the secret.

## Config Model

Config is schema-owned in `types.cppm`. Secret-bearing fields are marked
`.secret()` and must be redacted by generated config, effective config,
diagnostics and exception context.

Example:

```yaml
secret-provider:
  secrets:
    - id: service/session-key
      kind: symmetric-key
      encoding: hex
      source:
        type: env
        name: SERVICE_SESSION_KEY
      purposes:
        - api.payload.decrypt
      operations:
        - decrypt-aes-gcm
        - derive-hkdf-sha256
      allow-raw-export: false
```

Supported v1 source types:

- `inline`: development/test and explicitly allowed deployments only;
- `env`: process environment through FCL config/env loading;
- `file`: mounted secret file such as `/run/secrets/...`;
- `encrypted-file`: local encrypted secret store unlocked by passphrase from
  env or file, using FCL crypto primitives.

Deferred source types:

- cloud KMS providers;
- OS Keychain, Secure Enclave, TPM and hardware-backed adapters;
- interactive unlock UI;
- multi-party recovery and rotation campaigns.

These are new source backends, not changes to consumer APIs.

## Boundaries

`secret_provider` owns:

- config decoding and secret source loading;
- source-specific validation;
- redacted status and diagnostics;
- purpose and operation allow-list checks;
- bounded AES-GCM encrypt/decrypt through `fcl_crypto`;
- bounded HKDF-SHA256 derivation through `fcl_crypto`;
- optional raw export policy.

`secret_provider` does not own:

- product authority, grants, ACLs or workspace semantics;
- Spring, blockchain, storage, billing or deployment decisions;
- remote KMS protocols in v1;
- a wallet UI;
- a daemon-global singleton outside the application plugin registry;
- background workers, schedulers or network listeners.

## Implementation Blocks

1. Add `secret_provider` public DTOs, config schema and exceptions.
2. Add the local-only API and plugin descriptor.
3. Implement source loading for `inline`, `env` and `file` with redaction tests.
4. Implement `encrypted-file` source with AES-GCM and scrypt/HKDF primitives
   from `fcl_crypto`.
5. Implement purpose and operation enforcement.
6. Implement AES-GCM encrypt/decrypt, HKDF-SHA256 derive and gated raw export.
7. Add docs and plugin README integration.
8. Add package install smoke coverage like `signature_provider`.

## Test Plan

Unit tests:

- descriptor/config defaults and generated config redaction;
- inline/env/file sources load only through schema-described config;
- encrypted-file roundtrip, wrong passphrase, wrong AAD, wrong tag and corrupt
  ciphertext fail typed;
- unknown secret id, denied purpose and denied operation fail typed;
- `allow-raw-export=false` rejects `get_bytes`;
- AES-GCM encrypt/decrypt uses configured key and bounded AAD/ciphertext sizes;
- HKDF-SHA256 derives stable bytes for the same context and rejects invalid
  output sizes;
- status does not expose raw secret material.

Package tests:

- external consumer can import `fcl.plugins.secret_provider.*`, register the
  plugin, call `decrypt_aes_gcm` and receive typed failures.

Static gates:

```bash
rg "std::getenv|getenv\\(|OPENSSL|EVP_|private_key|workspace|spring|storlane" \
  plugins/secret_provider -g "*.cpp" -g "*.cppm" -g "*.hxx"

rg "secret-provider:.*value|<redacted>" docs plugins/secret_provider \
  -g "*.md" -g "*.cpp" -g "*.cppm" -g "*.hxx"
```

Validation:

```bash
cmake --build build/fcl-debug -j 1 \
  --target fcl_plugin_secret_provider test_fcl_plugins \
           test_fcl_crypto test_fcl_config test_fcl_env \
           test_fcl_package_plugin_secret_provider

ctest --test-dir build/fcl-debug --output-on-failure \
  -R "^(test_fcl_plugins|test_fcl_crypto|test_fcl_config|test_fcl_env|test_fcl_package_plugin_secret_provider)$" \
  --timeout 300

git diff --check
```

## Acceptance Criteria

- Products can configure secrets through FCL config/env/file sources without
  every product plugin parsing those sources directly.
- Consumers call operations by `secret_id` and `purpose`.
- Raw secret export is opt-in per secret and denied by default.
- Secret-bearing config and diagnostics are redacted.
- The plugin is neutral FCL infrastructure and contains no Storlane-specific
  names or semantics.
