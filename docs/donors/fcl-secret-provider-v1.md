# FCL Secret Provider v1 Donor Traceability

`fcl::plugins::secret_provider` is a planned neutral infrastructure plugin for
local secret material and bounded cryptographic operations. It complements
`signature_provider`: signing keys stay with the signer, while symmetric/data
secrets get a similar key-id, purpose and redaction boundary.

## Donor Matrix

| Donor | Accepted Pattern | Rejected Shortcut | FCL Target | Test Mapping |
| --- | --- | --- | --- | --- |
| [HashiCorp Vault Transit](https://developer.hashicorp.com/vault/docs/secrets/transit) | Operation API over named keys: encrypt, decrypt, sign, HMAC and random generation without consumers owning long-lived key storage. | Depending on Vault or copying its server model into FCL. | `secret_provider` exposes operation methods by `secret_id` and `purpose`; no remote service in v1. | Purpose denial, operation denial, decrypt/encrypt roundtrip, raw export denied by default. |
| [Vault Transit API](https://developer.hashicorp.com/vault/api-docs/secret/transit) | Explicit per-operation request/response shapes and key names. | One generic "do crypto" endpoint with untyped payloads. | Typed `aead_decrypt_request`, `derive_request`, `get_request`, and typed failures. | API descriptor tests and package consumer smoke. |
| [Kubo/IPFS keystore spec](https://github.com/ipfs/kubo/blob/master/docs/specifications/keystore.md) | Named key references and normal operations by ref instead of printing raw key bytes. | A daemon-global magic singleton or status/list commands exposing raw keys. | Stable `secret_id` values, redacted status, and no raw output except explicit opt-in. | Status redaction and `allow-raw-export=false` tests. |
| [Kubernetes Secrets](https://kubernetes.io/docs/concepts/configuration/secret/) | Secrets can be delivered to a daemon via mounted files or environment variables. | Treating environment variables or mounted files as a cryptographic boundary. | `env` and `file` source types are delivery sources behind schema/redaction. | Env/file load tests and generated config redaction tests. |
| [SOPS](https://getsops.io/docs/) | Encrypted YAML/JSON/ENV/INI/BINARY files are useful in CI/CD and GitOps flows. | Turning FCL into a SOPS clone or adding cloud KMS dependencies in v1. | `encrypted-file` source gives a local encrypted store; cloud KMS stays a future source backend. | Encrypted-file roundtrip, wrong passphrase and corrupt file tests. |
| [AWS KMS envelope encryption](https://docs.aws.amazon.com/encryption-sdk/latest/developer-guide/concepts.html) | Envelope encryption separates wrapping keys from data keys and makes key unwrapping explicit. | Baking AWS, IAM, grants or cloud-specific terminology into FCL. | `derive_hkdf_sha256` and AES-GCM operations support downstream envelope-style workflows. | Derivation stability and AEAD wrong AAD/tag tests. |
| FCL `signature_provider` | Local-only plugin, key ids, purpose allow-list, schema-owned secret config and output profile checks. | Product plugins parsing private keys, secret env vars or crypto profiles directly. | `secret_provider` follows the same official plugin shape and local API style. | Plugin descriptor, schema, purpose denial and package install tests. |

## Security Model

`secret_provider` does not make a compromised process safe. If a product daemon
has legitimate decrypt capability and an attacker gains arbitrary code execution
inside that daemon, the attacker can ask the provider to perform allowed
operations.

The provider instead reduces accidental and architectural exposure:

- secret material is not copied into every product plugin;
- generated and effective config can redact secret-bearing fields centrally;
- logs, status and exceptions can avoid raw key material;
- purpose and operation checks are enforced in one place;
- source backends can later move from env/file to encrypted-file, OS keychain or
  KMS without changing consumer APIs.

## Naming

The plugin name is `secret_provider`, not `vault`.

Rationale:

- "provider" matches existing `signature_provider` and describes the FCL API
  boundary.
- "vault" suggests a product storage/recovery/security authority and can imply
  HashiCorp Vault compatibility.
- FCL should provide neutral secret operations; products decide whether a
  configured secret represents a workspace secret, service token, data key or
  deployment credential.

## v1 Scope

Included:

- config schema and redaction;
- source types: `inline`, `env`, `file`, `encrypted-file`;
- key kinds: symmetric bytes for AES-GCM/HKDF workflows;
- local-only API through `fcl_api`;
- purpose and operation allow-lists;
- redacted status and typed failures.

Excluded:

- remote HashiCorp Vault, AWS KMS, GCP KMS, Azure Key Vault or HSM adapters;
- OS Keychain, Secure Enclave, TPM and smartcard adapters;
- interactive wallet UI or unlock prompts;
- product-specific authority, grants, ACLs, workspace recovery or rotation
  campaigns;
- raw OpenSSL use in plugin code paths when `fcl_crypto` already owns the
  primitive.

## Expected Downstream Use

A product plugin should depend on the API and call:

```text
decrypt_aes_gcm(secret_id, purpose, nonce, aad, ciphertext, tag)
derive_hkdf_sha256(secret_id, purpose, salt, info, output_size)
```

It should not parse `std::getenv`, read `/run/secrets`, decode encrypted files
or own local secret-redaction rules itself.
