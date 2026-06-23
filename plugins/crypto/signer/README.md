# Signing Provider Plugin

`fcl::plugins::crypto::signer` publishes a local-only typed API for signing
digests with configured private keys and output encoding profiles.

## Identity

- Target: `fcl_plugins_crypto_signer`
- Package component: `plugins_crypto_signer`
- Plugin id: `fcl.plugins.crypto.signer`
- Main API id: `fcl.plugins.crypto.signer`
- Config section: `plugins.crypto.signer`
- Public modules:
  - `fcl.plugins.crypto.signer.plugin`
  - `fcl.plugins.crypto.signer.api`
  - `fcl.plugins.crypto.signer.types`
  - `fcl.plugins.crypto.signer.exceptions`

## What It Provides

- Loads configured local private keys through `fcl_crypto`.
- Enforces key ids, allowed purposes, required algorithms and output profiles.
- Signs `fcl::crypto::sha256` digests through a local-only `fcl_api` contract.
- Keeps key material config secret/redacted through schema/config metadata.

It is not a wallet, vault, hardware security module or authorization layer. It
does not decide what a payload means; it only signs allowed digests with
configured keys.

## Config

```yaml
plugins:
   crypto:
      signer:
         default-output-profile: fcl
         keys:
            - id: service-key
              private-key: "<redacted private key>"
              input-profile: fcl
              purposes: ["api.receipt"]
```

`keys` is a secret object-list field. Load it from a protected config source;
do not rely on generated CLI or environment options for key material.

## Example

```cpp
import fcl.plugins.crypto.signer.api;
import fcl.plugins.crypto.signer.plugin;

auto signer = context.apis().get<fcl::plugins::crypto::signer::api>(
   {.id = {"fcl.plugins.crypto.signer"}, .major = 1});

auto result = co_await signer->sign(
   fcl::plugins::crypto::signer::request{
      .key_id = "service-key",
      .purpose = "api.receipt",
      .digest = digest,
      .required_algorithm =
         fcl::plugins::crypto::signer::key_algorithm::secp256k1,
      .output_profile = "fcl",
   });
```

```cpp
registry.register_plugin(fcl::plugins::crypto::signer::descriptor());
```
