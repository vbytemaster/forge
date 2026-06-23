# Signing Provider Plugin

`forge::plugins::crypto::signer` publishes a local-only typed API for signing
digests with configured private keys and output encoding profiles.

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

## Example

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
