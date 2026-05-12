# fcl_crypto

`fcl_crypto` contains retained cryptographic primitives and wrappers: hashes,
base encodings, AES/HMAC, AES-256-GCM, KDF helpers, K1/R1/WebAuthn keys and
signatures, BLS/BN/GMP helpers, random bytes and OpenSSL 3 integration.

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

- `fcl.crypto.types`, `random`, `kdf`, `aes`, `rand`,
  `bigint`, `modular_arithmetic`, `packhash`, `openssl`, `bls_*`, `common`.

Target: `fcl_crypto`.

Dependencies: `fcl_core`, `fcl_exception`, `fcl_raw`, `fcl_reflect`,
`fcl_variant`, OpenSSL::Crypto, GMP, secp256k1 and BLS vendor code.

## Examples

### Hash Data And Stream Into An Encoder

```cpp
import fcl.crypto.sha256;

auto digest = fcl::sha256::hash("payload");
auto hex = digest.str();

auto encoder = fcl::sha256::encoder{};
encoder.write("pay", 3);
encoder.write("load", 4);
auto streaming_digest = encoder.result();
```

### Compare Hash Families

```cpp
import fcl.crypto.ripemd160;
import fcl.crypto.sha1;
import fcl.crypto.sha256;
import fcl.crypto.sha512;

auto sha1 = fcl::sha1::hash("abc").str();
auto sha256 = fcl::sha256::hash("abc").str();
auto sha512 = fcl::sha512::hash("abc").str();
auto ripemd = fcl::ripemd160::hash("abc").str();
```

### Encode And Decode Bytes

```cpp
#include <vector>

import fcl.crypto.base58;
import fcl.crypto.base64;
import fcl.crypto.hex;

auto bytes = std::vector<char>{'o', 'k'};

auto hex = fcl::to_hex(bytes);
auto base58 = fcl::to_base58(bytes, {});
auto base64 = fcl::base64_encode(bytes);
auto base64url = fcl::base64url_encode("hello");

auto decoded_base58 = fcl::from_base58(base58);
auto decoded_base64 = fcl::base64_decode(base64);
```

### Generate Random Bytes For A Seed Or Key

```cpp
#include <array>

import fcl.crypto.random;
import fcl.crypto.rand;

auto seed = std::array<char, 32>{};      // retained low-level FC-compatible API
fcl::rand_bytes(seed.data(), static_cast<int>(seed.size()));

auto token = fcl::crypto::random_bytes(32);
auto nonce = fcl::crypto::random_array<12>();
auto key = fcl::crypto::generate_aes256_key();
```

Random bytes are secret until proven otherwise. Do not log generated material;
if you need to show an identifier, derive and render a public fingerprint.

### Generate And Use A K1 Key

```cpp
import fcl.crypto.private_key;
import fcl.crypto.public_key;
import fcl.crypto.sha256;
import fcl.crypto.signature;

auto private_key = fcl::crypto::private_key::generate();
auto public_key = private_key.get_public_key();
auto digest = fcl::sha256::hash("message");
auto signature = private_key.sign(digest);
auto recovered_public_key = fcl::crypto::public_key{signature, digest};
auto verified = recovered_public_key == public_key;
```

### Generate An R1 Key Explicitly

```cpp
import fcl.crypto.elliptic_r1;
import fcl.crypto.private_key;
import fcl.crypto.public_key;
import fcl.crypto.sha256;

auto private_key = fcl::crypto::private_key::generate<fcl::crypto::r1::private_key_shim>();
auto digest = fcl::sha256::hash("message");
auto signature = private_key.sign(digest);
auto public_key = private_key.get_public_key();
auto recovered_public_key = fcl::crypto::public_key{signature, digest};
```

K1 and R1 have different compatibility expectations. Keep tests for both when
changing shared signature code.

### Parse Existing Key Strings

```cpp
import fcl.crypto.private_key;
import fcl.crypto.public_key;

auto private_key = fcl::crypto::private_key{
   "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
auto public_key = private_key.get_public_key();

auto public_text = public_key.to_string({});
```

`private_key::to_string()` returns private key material. It is useful for test
fixtures and import/export flows, not for logs.

### HMAC With SHA-256

```cpp
#include <cstdint>
#include <string>

import fcl.crypto.hmac;

auto key = std::string{"local-test-key"};
auto payload = std::string{"payload"};

auto hmac = fcl::hmac_sha256{};
auto digest = hmac.digest(
   key.data(),
   static_cast<std::uint32_t>(key.size()),
   payload.data(),
   static_cast<std::uint32_t>(payload.size()));
```

### Derive Material With HKDF-SHA256

```cpp
import fcl.crypto.kdf;

auto material = fcl::crypto::derive_hkdf_sha256({
   .secret = {'w', 'o', 'r', 'k', 's', 'p', 'a', 'c', 'e'},
   .salt = {'s', 'a', 'l', 't'},
   .info = {'c', 'h', 'u', 'n', 'k', '-', 'k', 'e', 'y'},
   .output_size = fcl::crypto::aes256_key_size,
});
```

HKDF is deterministic: the same secret, salt and info produce the same output.
Callers own domain separation for `info`; do not reuse the same tuple for
unrelated keys.

### Derive Material With Scrypt

```cpp
import fcl.crypto.kdf;
import fcl.crypto.random;

auto vault_key = fcl::crypto::derive_scrypt({
   .password = "operator supplied passphrase",
   .salt = fcl::crypto::random_bytes(16),
   .n = 16'384,
   .r = 8,
   .p = 1,
   .max_memory_bytes = 32ULL * 1024ULL * 1024ULL,
   .output_size = fcl::crypto::aes256_key_size,
});
```

Scrypt parameters are policy, not magic constants. Products must choose them
for their latency and memory budget and keep salts non-secret but unique.

### AES-256-GCM Authenticated Encryption

```cpp
import fcl.crypto.aes;
import fcl.crypto.random;

auto key = fcl::crypto::generate_aes256_key();
auto nonce = fcl::crypto::random_bytes(fcl::crypto::aes_gcm_nonce_size);

auto encrypted = fcl::crypto::encrypt_aes256_gcm({
   .key = key,
   .nonce = nonce,
   .plaintext = {'s', 'e', 'c', 'r', 'e', 't'},
   .aad = {'m', 'e', 't', 'a'},
});

auto plaintext = fcl::crypto::decrypt_aes256_gcm({
   .key = key,
   .encrypted = encrypted,
   .aad = {'m', 'e', 't', 'a'},
});
```

AES-GCM requires nonce uniqueness per key. FCL validates sizes and tag
authentication, but callers own key lifecycle, nonce policy and secret
redaction.

### AES-256-CBC Compatibility Helper

```cpp
import fcl.crypto.aes;
import fcl.crypto.random;

auto key = fcl::crypto::generate_aes256_key();
auto iv = fcl::crypto::random_bytes(fcl::crypto::aes_cbc_iv_size);

auto cipher = fcl::crypto::encrypt_aes256_cbc({
   .key = key,
   .iv = iv,
   .plaintext = {'s', 'e', 'c', 'r', 'e', 't'},
});

auto roundtrip = fcl::crypto::decrypt_aes256_cbc({
   .key = key,
   .encrypted = cipher,
});
```

CBC is retained for compatibility-oriented payloads. Prefer AES-256-GCM for new
authenticated encryption; CBC does not authenticate ciphertext by itself.

### Hash Structured Data With Raw-Compatible Packing

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

struct signed_payload {
   std::uint64_t nonce = 0;
   std::string body;
};

BOOST_DESCRIBE_STRUCT(signed_payload, (), (nonce, body))

import fcl.crypto.sha256;
import fcl.raw.raw;

auto digest = fcl::sha256::hash(signed_payload{.nonce = 7, .body = "approve"});
```

`sha256::hash(T)` uses `fcl::raw::pack`, so field order is binary
compatibility-sensitive.

### Serialize Through Variant Without Revealing Secrets

```cpp
import fcl.crypto.private_key;
import fcl.crypto.public_key;
import fcl.variant;

auto private_key = fcl::crypto::private_key::generate();
auto public_key = private_key.get_public_key();

fcl::variant rendered_public;
fcl::to_variant(public_key, rendered_public);

// Avoid this outside explicit import/export tooling:
// fcl::to_variant(private_key, rendered_secret);
```

Variant conversion is for stable data exchange and tests. It is not automatic
redaction.

### BLS Sign And Verify

```cpp
#include <cstdint>
#include <vector>

import fcl.crypto.bls_private_key;
import fcl.crypto.bls_public_key;
import fcl.crypto.bls_signature;
import fcl.crypto.bls_utils;

using namespace fcl::crypto::blslib;

auto seed = std::vector<std::uint8_t>{
   0, 50, 6, 244, 24, 199, 1, 25,
   52, 88, 192, 19, 18, 12, 89, 6,
   220, 18, 102, 58, 209, 82, 12, 62,
   89, 110, 182, 9, 44, 20, 254, 22};
auto message = std::vector<std::uint8_t>{1, 2, 3, 4};

auto private_key = bls_private_key{seed};
auto public_key = private_key.get_public_key();
auto signature = private_key.sign(message);
auto ok = verify(public_key, message, signature);
```

BLS values have compatibility-specific binary and string forms. Keep generated
strings and packed bytes covered by tests when changing wrappers.

### BLAKE2 With A Progress Callback

```cpp
import fcl.crypto.blake2;

auto yield = fcl::yield_function_t{[] {
   // progress/deadline checkpoint
}};
auto digest = fcl::blake2b(rounds, h, message, t0_offset, t1_offset, final_block, yield);
```

### WebAuthn Boundary

```cpp
import fcl.crypto.elliptic_webauthn;
import fcl.crypto.sha256;

auto digest = fcl::sha256::hash("challenge");
auto recovered = webauthn_signature.recover(digest, true);
```

WebAuthn client data parsing is private to `fcl_crypto`; no JSON backend type is
part of the public API. Keep high-S WebAuthn recovery and canonical R1 recovery
tests together when touching this code.

## Security Notes

- `private_key::to_string()` is secret material. Do not print it in diagnostics.
- Use explicit redaction in config/log/TUI layers before rendering crypto values.
- OpenSSL 3 is the only SSL/TLS-related crypto backend expected by FCL product
  builds.
- `fcl_crypto` is synchronous and does not import runtime schedulers; async
  scheduling belongs in caller or adapter layers.
- Canonical signature behavior is compatibility-sensitive; changes require
  regression tests for low/high-s and WebAuthn cases.

## Typical Mistakes

- Do not weaken canonical signature checks to make tests pass.
- Do not introduce another TLS backend through crypto dependencies.
- Do not put certificate issuance or identity enrollment product flows in
  `fcl_crypto`; this library provides primitives, not workflows.

## Tests

`test_fcl_crypto` covers hash vectors, base64/base58/hex, random bytes,
HKDF/scrypt, AES-256-GCM roundtrip/authentication failure, K1/R1 signing and
recovery, WebAuthn canonical checks, BLS serialization/verification, modular
arithmetic and BLAKE2 vectors.
