# Secret Provider Plugin

`fcl::plugins::crypto::secrets` provides a local-only secret operations API for
`fcl_app` applications. It lets application plugins request bounded secret
retrieval, HKDF-SHA256 derivation and AES-GCM encryption/decryption by
`secret_id` and `purpose`, without parsing secret-bearing config or encrypted
files in product code.

The plugin is not a wallet, remote KMS, product vault or authorization service.
It enforces configured purposes, operation allow-lists, byte limits and local
source loading policy, then delegates cryptographic primitives to `fcl_crypto`.

## Target And Modules

- Target: `fcl_plugins_crypto_secrets`
- Package component: `plugins_crypto_secrets`
- Plugin id: `fcl.plugins.crypto.secrets`
- Main API contract id: `fcl.plugins.crypto.secrets`
- Config section: `plugins.crypto.secrets`

Public modules:

- `fcl.plugins.crypto.secrets.plugin`
- `fcl.plugins.crypto.secrets.api`
- `fcl.plugins.crypto.secrets.types`
- `fcl.plugins.crypto.secrets.exceptions`

## Configuration

Config is decoded through `BOOST_DESCRIBE_STRUCT`, `fcl_schema` rules and
`fcl_config`. Secret-bearing fields are schema-marked as secret so generated
diagnostics and redaction paths do not expose raw material.

```yaml
plugins:
  crypto:
    secrets:
      default-max-plaintext-bytes: 1048576
      default-max-ciphertext-bytes: 1048708
      default-max-aad-bytes: 1048576
      secrets:
        - id: service/session-key
          kind: symmetric_key
          source:
            type: value
            encoding: hex
            value: "<redacted secret bytes>"
          purposes: ["api.payload.decrypt"]
          operations: ["decrypt_aes_gcm", "derive_hkdf_sha256"]
          allow-raw-export: false
```

Supported source types are `value`, `file` and `encrypted_file`. The
`encrypted_file` source uses the plugin's encrypted secret file container helper
and configurable scrypt ceilings.

## Usage

Register the descriptor with the application shell and acquire the typed API
from the plugin context:

```cpp
registry.register_plugin(fcl::plugins::crypto::secrets::descriptor());
```

```cpp
auto secrets = context.apis().get<fcl::plugins::crypto::secrets::api>(
   {.id = {"fcl.plugins.crypto.secrets"}, .major = 1});

auto derived = co_await secrets->derive_hkdf_sha256(
   fcl::plugins::crypto::secrets::derive_request{
      .secret_id = "service/session-key",
      .purpose = "api.payload.decrypt",
      .salt = salt,
      .info = info,
      .output_size = 32,
   });
```

## Boundaries

- The plugin owns local secret loading, byte limits and operation gating.
- Products own policy: who may call the API, what a purpose means and where
  secret material originates operationally.
- Do not expose raw secrets unless the config explicitly sets
  `allow-raw-export: true` and the requested operation is allowed.
- Do not use this plugin as a remote vault or product authorization layer.
