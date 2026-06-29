# Signing Provider Plugin

`forge::plugins::crypto::signer` publishes a local-only typed API for signing
digests with configured private keys and output encoding profiles.

## When To Use

- A Forge application needs local signing through a plugin-owned API.
- Keys are configured locally and selected by `key_id` and purpose.
- Callers already have a digest and need an encoded signature result.

## When Not To Use

- Do not use this plugin as a wallet, remote KMS or authorization service.
- Do not pass raw product DTO strings for signing. Pack/hash the DTO in the
  consuming layer, then pass a digest.
- Do not expose private keys through generated examples, CLI flags or logs.

## Identity

- Target: `forge_plugins_crypto_signer`
- Package component: `plugins_crypto_signer`
- Plugin id: `forge.plugins.crypto.signer`
- Main API id: `forge.plugins.crypto.signer`
- Config section: `plugins.crypto.signer`
- Public modules:
  - `forge.plugins.crypto.signer.plugin`
  - `forge.plugins.crypto.signer.api`
  - `forge.plugins.crypto.signer.types`
  - `forge.plugins.crypto.signer.exceptions`

## What It Provides

- Loads configured local private keys through `forge_crypto`.
- Enforces key ids, allowed purposes, required algorithms and output profiles.
- Signs `forge::crypto::sha256` digests through a local-only `forge_api` contract.
- Keeps key material config secret/redacted through schema/config metadata.

It is not a wallet, vault, hardware security module or authorization layer. It
does not decide what a payload means; it only signs allowed digests with
configured keys.

## Dependencies

- `forge_app`
- `forge_api`
- `forge_crypto`
- `forge_config`
- `forge_schema`

## Config

```yaml
plugins:
   crypto:
      signer:
         default-output-profile: forge
         keys:
            - id: service-key
              private-key: "<redacted private key>"
              input-profile: forge
              purposes: ["api.receipt"]
```

`keys` is a secret object-list field. Load it from a protected config source;
do not rely on generated CLI or environment options for key material.

## Examples

```cpp
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.plugin;

auto signer = context.apis().get<forge::plugins::crypto::signer::api>(
   {.id = {"forge.plugins.crypto.signer"}, .major = 1});

auto result = co_await signer->sign(
   forge::plugins::crypto::signer::request{
      .key_id = "service-key",
      .purpose = "api.receipt",
      .digest = digest,
      .required_algorithm =
         forge::plugins::crypto::signer::key_algorithm::secp256k1,
      .output_profile = "forge",
   });
```

```cpp
registry.register_plugin(forge::plugins::crypto::signer::descriptor());
```

## Security And Boundaries

- Private key material is schema-marked secret and must be loaded from protected
  config sources.
- The plugin signs allowed digests only; it does not decide whether a payload is
  authorized.
- Purpose checks are plugin-local allow-lists. Product policy must define what a
  purpose means.

## Common Mistakes

- Signing JSON strings or manually concatenated fields. Prefer
  `Boost.Describe -> forge::raw::pack -> hash -> sign`.
- Reusing one key id for unrelated purposes without an explicit purpose list.
- Logging request structs that may contain key ids and operational context.

## Tests

- `test_forge_plugins`
- `test_forge_package_plugins_crypto_signer`
