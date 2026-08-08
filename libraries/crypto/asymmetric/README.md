# Forge Crypto Asymmetric

Asymmetric Crypto is split into a lightweight value component and a host
algorithm component. Public symbols remain directly in
`forge::crypto::asymmetric`; no `values`, `host` or `guest` namespace is added.

## Components

`forge_crypto_asymmetric_values` / `crypto_asymmetric_values` exports
`forge.crypto.asymmetric.values`: binary `algorithm`, `public_key` and
`signature` values plus their Raw contract. It does not depend on OpenSSL,
secp256k1 or RSA implementations.

`forge_crypto_asymmetric` / `crypto_asymmetric` exports:

- `forge.crypto.asymmetric`
- `forge.crypto.asymmetric.serialization`
- `forge.crypto.asymmetric.secp256k1`
- `forge.crypto.asymmetric.p256`
- `forge.crypto.asymmetric.ed25519`
- `forge.crypto.asymmetric.rsa`
- `forge.crypto.asymmetric.x25519`
- `forge.crypto.asymmetric.webauthn`

```cpp
#include <string>

import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;

const auto key = forge::crypto::asymmetric::private_key::generate_p256();
const auto digest = forge::crypto::digest::sha256::hash(std::string{"payload"});
const auto signature = key.sign_digest(digest);
```

The host component adds OpenSSL and dedicated K1 implementation dependencies.
`forge.crypto.asymmetric.serialization` is the narrow host-side Variant/JSON
surface for canonical public-key and signature strings. Protocol libraries may
re-export this module without exposing asymmetric signing operations.
Text profiles are explicit encoding boundaries; binary protocol models should
depend only on `crypto_asymmetric_values`. `test_forge_crypto_asymmetric_values`
locks value tags/bytes, while `test_forge_crypto_asymmetric` covers signing,
verification, recovery, WebAuthn and text encoding.
