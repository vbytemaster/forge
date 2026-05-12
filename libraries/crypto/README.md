# fcl_crypto

`fcl_crypto` contains retained cryptographic primitives and wrappers: hashes,
base encodings, AES/HMAC, K1/R1/WebAuthn keys and signatures, BLS/BN/GMP
helpers, random bytes and OpenSSL 3 integration.

## When To Use

- You need in-process key generation/sign/verify/hash primitives.
- You need retained K1/R1/WebAuthn/BLS compatibility behavior from FC/EOS-like
  ecosystems.
- You need crypto wrappers that can participate in `fcl_raw` and `fcl_variant`
  compatibility tests.

## When Not To Use

- Do not shell out to external `openssl` binaries for product key/cert flows.
- Do not log private keys, shared secrets, passphrases or seed material.
- Do not add product-specific key custody or wallet policy here.
- Do not treat `secp256k1` as an SSL/TLS backend; it is a signature library.

## Public Modules

Hashes and encodings:

- `fcl.crypto.sha1`, `sha224`, `sha256`, `sha3`, `sha512`, `ripemd160`, `city`,
  `blake2`, `hex`, `base58`, `base64`, `hmac`.

Keys and signatures:

- `fcl.crypto.private_key`, `public_key`, `signature`, `elliptic`,
  `elliptic_r1`, `elliptic_webauthn`, `k1_recover`.

Other primitives:

- `fcl.crypto.aes`, `rand`, `bigint`, `modular_arithmetic`, `packhash`,
  `openssl`, `bls_*`, `common`.

Target: `fcl_crypto`.

Dependencies: `fcl_core`, `fcl_exception`, `fcl_raw`, `fcl_reflect`,
`fcl_variant`, OpenSSL::Crypto, GMP, secp256k1 and BLS vendor code.

## Examples

### Hash Data

```cpp
import fcl.crypto.sha256;

auto digest = fcl::sha256::hash("payload");
auto hex = digest.str();
```

### Generate And Use A K1 Key

```cpp
import fcl.crypto.private_key;
import fcl.crypto.sha256;

auto private_key = fcl::crypto::private_key::generate();
auto public_key = private_key.get_public_key();
auto digest = fcl::sha256::hash("message");
auto signature = private_key.sign(digest);
auto recovered_public_key = fcl::crypto::public_key{signature, digest};
auto verified = recovered_public_key == public_key;
```

### Encode Binary Data

```cpp
import fcl.crypto.hex;

auto bytes = std::vector<char>{'o', 'k'};
auto text = fcl::to_hex(bytes);
```

### BLAKE2 With Cancellation Callback

```cpp
import fcl.crypto.blake2;

auto yield = fcl::yield_function_t{[] { /* progress/deadline checkpoint */ }};
auto digest = fcl::blake2b(rounds, h, message, t0_offset, t1_offset, final_block, yield);
```

## Security Notes

- `private_key::to_string()` is secret material. Do not print it in diagnostics.
- Use explicit redaction in config/log/TUI layers before rendering crypto values.
- OpenSSL 3 is the only SSL/TLS-related crypto backend expected by FCL product
  builds.
- Canonical signature behavior is compatibility-sensitive; changes require
  regression tests for low/high-s and WebAuthn cases.

## Typical Mistakes

- Do not weaken canonical signature checks to make tests pass.
- Do not introduce another TLS backend through crypto dependencies.
- Do not put certificate issuance or identity enrollment product flows in
  `fcl_crypto`; this library provides primitives, not workflows.

## Tests

`test_fcl_crypto` covers hash vectors, base64/base58/hex, K1/R1 signing and
recovery, WebAuthn canonical checks, BLS serialization/verification, modular
arithmetic and BLAKE2 vectors.
