# Signing Provider Plugin

`fcl::plugins::signing::provider` publishes a local-only typed API for signing
digests with configured private keys and output encoding profiles.

## Identity

- Target: `fcl_plugins_signing_provider`
- Package component: `plugins_signing_provider`
- Plugin id: `fcl.plugins.signing.provider`
- Main API id: `fcl.plugins.signing.provider`
- Config section: `plugins.signing.provider`
- Public modules:
  - `fcl.plugins.signing.provider.plugin`
  - `fcl.plugins.signing.provider.api`
  - `fcl.plugins.signing.provider.types`
  - `fcl.plugins.signing.provider.exceptions`

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
   signing:
      provider:
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
import fcl.plugins.signing.provider.api;
import fcl.plugins.signing.provider.plugin;

auto signer = context.apis().get<fcl::plugins::signing::provider::api>(
   {.id = {"fcl.plugins.signing.provider"}, .major = 1});

auto result = co_await signer->sign(
   fcl::plugins::signing::provider::request{
      .key_id = "service-key",
      .purpose = "api.receipt",
      .digest = digest,
      .required_algorithm =
         fcl::plugins::signing::provider::key_algorithm::secp256k1,
      .output_profile = "fcl",
   });
```

```cpp
registry.register_plugin(fcl::plugins::signing::provider::descriptor());
```
