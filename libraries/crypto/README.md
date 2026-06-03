# fcl_crypto

`fcl_crypto` contains retained cryptographic primitives and wrappers: hashes,
base encodings, AES/HMAC, AES-256-GCM, KDF helpers, secp256k1/P-256/WebAuthn keys
and signatures, BLS/BN/GMP helpers, random bytes and OpenSSL 3.0+ integration.

## When To Use

- You need in-process key generation/sign/verify/hash primitives.
- You need retained secp256k1/P-256/WebAuthn/BLS compatibility behavior from
  FC/EOS-like ecosystems.
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

- `fcl.crypto.asymmetric`, `secp256k1`, `p256`, `ed25519`, `rsa`,
  `x25519`, `webauthn`, `bls`.

Other primitives:

- `fcl.crypto.types`, `random`, `kdf`, `aes`,
  `bigint`, `modular_arithmetic`, `packhash`, `pem`, `der`, `x509`.

Target: `fcl_crypto`.

Dependencies: `fcl_core`, `fcl_exceptions`, `fcl_raw`, `fcl_reflect`,
`fcl_variant`, OpenSSL::Crypto, GMP, secp256k1 and BLS vendor code.

## Examples

### Hash A Domain Object With Raw-Compatible Bytes

Product protocols should hash stable binary payloads, not ad-hoc strings. The
recommended pattern is: describe the DTO, keep member order stable, pack it with
`fcl::raw::pack` directly into a hash encoder, then sign that digest if needed.

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.crypto.sha256;
import fcl.raw.raw;

struct transfer_digest_payload {
   std::uint64_t account = 0;
   std::uint64_t sequence = 0;
   std::string memo;

   [[nodiscard]] fcl::crypto::sha256 digest() const;
};

BOOST_DESCRIBE_STRUCT(transfer_digest_payload, (), (account, sequence, memo))

inline fcl::crypto::sha256 transfer_digest_payload::digest() const {
   auto encoder = fcl::crypto::sha256::encoder{};
   fcl::raw::pack(encoder, *this);
   return encoder.result();
}

auto payload = transfer_digest_payload{
   .account = 42,
   .sequence = 7,
   .memo = "approve",
};

auto digest = payload.digest();
auto hex = digest.str();
```

`BOOST_DESCRIBE_STRUCT` order is the hash contract. Reordering fields changes
the digest and breaks signatures, caches and contract compatibility.

### Hash Test Vectors And Byte Streams

String hashing is useful for vectors, probes and small tooling. Do not copy this
as the product protocol pattern when the payload is a C++ DTO.

```cpp
import fcl.crypto.sha256;

auto digest = fcl::crypto::sha256::hash("payload");
auto hex = digest.str();

auto encoder = fcl::crypto::sha256::encoder{};
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

auto sha1 = fcl::crypto::sha1::hash("abc").str();
auto sha256 = fcl::crypto::sha256::hash("abc").str();
auto sha512 = fcl::crypto::sha512::hash("abc").str();
auto ripemd = fcl::crypto::ripemd160::hash("abc").str();
```

### Encode And Decode Bytes

```cpp
#include <vector>

import fcl.crypto.base58;
import fcl.crypto.base64;
import fcl.crypto.hex;
import fcl.crypto.types;

auto bytes = fcl::crypto::bytes{'o', 'k'};

auto hex = fcl::crypto::to_hex(bytes);
auto base58 = fcl::crypto::base58_encode(bytes);
auto base64 = fcl::crypto::base64_encode(bytes);
auto base64url = fcl::crypto::base64url_encode("hello");

auto decoded_base58 = fcl::crypto::base58_decode(base58);
auto decoded_base64 = fcl::crypto::base64_decode(base64);
```

### Generate Random Bytes For A Seed Or Key

```cpp
#include <array>

import fcl.crypto.random;
import fcl.crypto.aes;

auto token = fcl::crypto::random_bytes(32);
auto nonce = fcl::crypto::random_array<12>();
auto key = fcl::crypto::generate_aes256_key();
```

Random bytes are secret until proven otherwise. Do not log generated material;
if you need to show an identifier, derive and render a public fingerprint.

### Generate And Use A Secp256k1 Key

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.crypto.asymmetric;
import fcl.crypto.sha256;
import fcl.crypto.types;
import fcl.raw.raw;

struct signed_action {
   std::uint64_t actor = 0;
   std::uint64_t nonce = 0;
   std::string operation;

   [[nodiscard]] fcl::crypto::bytes signing_bytes(const fcl::crypto::sha256& chain_id) const;
};

BOOST_DESCRIBE_STRUCT(signed_action, (), (actor, nonce, operation))

inline fcl::crypto::bytes signed_action::signing_bytes(const fcl::crypto::sha256& chain_id) const {
   auto bytes = fcl::crypto::bytes{};
   fcl::raw::pack(bytes, chain_id);
   fcl::raw::pack(bytes, *this);
   return bytes;
}

auto private_key = fcl::crypto::asymmetric::private_key::generate();
auto public_key = private_key.get_public_key();

auto chain_id = fcl::crypto::sha256{}; // Replace with the real chain/domain id.
auto action = signed_action{
   .actor = 42,
   .nonce = 9,
   .operation = "grant",
};

auto message = action.signing_bytes(chain_id);
auto signature = private_key.sign(message);
auto verified = public_key.verify(message, signature);
```

### Generate A P-256 Key Explicitly

This snippet reuses the `signed_action` DTO from the secp256k1 example above.
Keep the same packed payload shape when comparing secp256k1/P-256 behavior.

```cpp
import fcl.crypto.p256;
import fcl.crypto.sha256;
import fcl.raw.raw;

auto private_key = fcl::crypto::asymmetric::private_key::generate_p256();
auto public_key = private_key.get_public_key();

auto chain_id = fcl::crypto::sha256{}; // Replace with the real chain/domain id.
auto action = signed_action{
   .actor = 42,
   .nonce = 10,
   .operation = "webauthn-check",
};

auto message = action.signing_bytes(chain_id);
auto signature = private_key.sign(message);
auto verified = public_key.verify(message, signature);
```

Secp256k1 and P-256 have different compatibility expectations. Keep tests for
both when changing shared signature code.

Signature anti-patterns:

- Do not sign JSON, YAML, pretty-printed strings or manually concatenated
  fields for protocol authorization.
- Do not verify against bytes reconstructed from a different DTO or a different
  `BOOST_DESCRIBE_STRUCT` field order.
- Do not accept a recoverable signature just because recovery succeeded; compare
  the recovered public key with the expected signer.

### Parse Existing Key Strings

```cpp
import fcl.crypto.asymmetric;

auto generated = fcl::crypto::asymmetric::private_key::generate();
auto private_text = fcl::crypto::asymmetric::encoding::fcl().format(generated);

auto private_key = fcl::crypto::asymmetric::encoding::fcl().parse_private(private_text);
auto public_text = fcl::crypto::asymmetric::encoding::fcl().format(private_key.get_public_key());
```

The generic parser accepts only the canonical FCL profile. EOS/FC-like key text
must go through `fcl::crypto::asymmetric::encoding::eos()` explicitly.
`private_key::to_string()` returns private key material. It is useful for test
fixtures and import/export flows, not for logs.

### HMAC With SHA-256

```cpp
#include <cstdint>
#include <string>

import fcl.crypto.hmac;

auto key = std::string{"local-test-key"};
auto payload = std::string{"payload"};

auto hmac = fcl::crypto::hmac_sha256{};
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
   .output_size = fcl::crypto::default_derived_key_size,
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
   .output_size = fcl::crypto::default_derived_key_size,
});
```

Scrypt parameters are policy, not magic constants. Products must choose them
for their latency and memory budget and keep salts non-secret but unique.

### AES-256-GCM Authenticated Encryption

One-shot encryption is fine for small already-materialized buffers, such as a
config secret after validation. For product DTOs and large payloads, prefer the
streaming encoder below so `fcl::raw::pack` writes directly into authenticated
encryption.

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

### Stream Raw-Compatible Data Into AES-256-GCM

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.crypto.aes;
import fcl.crypto.random;
import fcl.raw.raw;

struct sealed_payload {
   std::uint64_t nonce = 0;
   std::string body;
};

BOOST_DESCRIBE_STRUCT(sealed_payload, (), (nonce, body))

auto ciphertext = fcl::crypto::bytes{};
auto key = fcl::crypto::generate_aes256_key();
auto nonce = fcl::crypto::random_bytes(fcl::crypto::aes_gcm_nonce_size);

auto encoder = fcl::crypto::aes256_gcm_encoder{
   fcl::crypto::aes256_gcm_encoder_options{
      .key = key,
      .nonce = nonce,
      .aad = {'m', 'e', 't', 'a'},
      .ciphertext_sink = [&](std::span<const std::uint8_t> chunk) {
         ciphertext.insert(ciphertext.end(), chunk.begin(), chunk.end());
      },
   }};

fcl::raw::pack(encoder, sealed_payload{.nonce = 7, .body = "large payload"});
auto authentication = encoder.finalize();
```

`aes256_gcm_encoder` exposes `write(const char*, std::size_t)` so it can be used
as a raw pack stream, like hash encoders. The sink receives ciphertext chunks as
OpenSSL produces them; callers do not need to materialize all plaintext first.

### Stream AES-256-GCM Decryption

```cpp
import fcl.crypto.aes;

auto plaintext = fcl::crypto::bytes{};
auto decoder = fcl::crypto::aes256_gcm_decoder{
   fcl::crypto::aes256_gcm_decoder_options{
      .key = key,
      .nonce = authentication.nonce,
      .tag = authentication.tag,
      .aad = {'m', 'e', 't', 'a'},
      .plaintext_sink = [&](std::span<const std::uint8_t> chunk) {
         plaintext.insert(plaintext.end(), chunk.begin(), chunk.end());
      },
   }};

decoder.write(ciphertext.data(), ciphertext.size());
decoder.finalize();
```

Plaintext emitted before `finalize()` is provisional. For files, write to a
temporary path and only commit or rename after tag verification succeeds.

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

import fcl.crypto.sha256;
import fcl.raw.raw;

struct signed_payload {
   std::uint64_t nonce = 0;
   std::string body;

   [[nodiscard]] fcl::crypto::sha256 digest() const;
};

BOOST_DESCRIBE_STRUCT(signed_payload, (), (nonce, body))

inline fcl::crypto::sha256 signed_payload::digest() const {
   auto encoder = fcl::crypto::sha256::encoder{};
   fcl::raw::pack(encoder, *this);
   return encoder.result();
}

auto digest = signed_payload{.nonce = 7, .body = "approve"}.digest();
```

The encoder path is the same path used by `fcl::raw::pack`, so field order is
binary compatibility-sensitive.

### Serialize Through Variant Without Revealing Secrets

```cpp
import fcl.variant;

auto private_key = fcl::crypto::asymmetric::private_key::generate();
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
#include <boost/describe.hpp>

#include <cstdint>
#include <string>
#include <vector>

import fcl.crypto.bls;
import fcl.crypto.sha256;
import fcl.raw.raw;

using namespace fcl::crypto::bls;

struct bls_vote_payload {
   std::uint64_t round = 0;
   std::string decision;

   [[nodiscard]] fcl::crypto::sha256 digest() const;
};

BOOST_DESCRIBE_STRUCT(bls_vote_payload, (), (round, decision))

inline fcl::crypto::sha256 bls_vote_payload::digest() const {
   auto encoder = fcl::crypto::sha256::encoder{};
   fcl::raw::pack(encoder, *this);
   return encoder.result();
}

auto seed = std::vector<std::uint8_t>{
   0, 50, 6, 244, 24, 199, 1, 25,
   52, 88, 192, 19, 18, 12, 89, 6,
   220, 18, 102, 58, 209, 82, 12, 62,
   89, 110, 182, 9, 44, 20, 254, 22};
auto message_digest = bls_vote_payload{
   .round = 12,
   .decision = "commit",
}.digest();
auto message = std::vector<std::uint8_t>{
   message_digest.to_uint8_span().begin(),
   message_digest.to_uint8_span().end(),
};

auto key = private_key{seed};
auto public_key = key.get_public_key();
auto signature = key.sign(message);
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
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.crypto.webauthn;
import fcl.crypto.sha256;
import fcl.raw.raw;

struct webauthn_challenge_payload {
   std::uint64_t session = 0;
   std::string origin;

   [[nodiscard]] fcl::crypto::sha256 digest() const;
};

BOOST_DESCRIBE_STRUCT(webauthn_challenge_payload, (), (session, origin))

inline fcl::crypto::sha256 webauthn_challenge_payload::digest() const {
   auto encoder = fcl::crypto::sha256::encoder{};
   fcl::raw::pack(encoder, *this);
   return encoder.result();
}

auto digest = webauthn_challenge_payload{
   .session = 42,
   .origin = "https://example.invalid",
}.digest();
auto recovered = webauthn_signature.recover(digest, true);
```

WebAuthn client data parsing is private to `fcl_crypto`; no JSON backend type is
part of the public API. Keep high-S WebAuthn recovery and canonical P-256
recovery tests together when touching this code.

## Security Notes

- `private_key::to_string()` is secret material. Do not print it in diagnostics.
- Use explicit redaction in config/log/TUI layers before rendering crypto values.
- OpenSSL 3.0+ is the only SSL/TLS-related crypto backend expected by FCL product
  builds.
- `fcl_crypto` is synchronous and does not import runtime schedulers; async
  scheduling belongs in caller or adapter layers.
- Canonical signature behavior is compatibility-sensitive; changes require
  regression tests for low/high-s and WebAuthn cases.

## Runtime Risks And Anti-Patterns

- Do not hash `std::format(...)`, JSON text or manually joined strings for
  protocol signatures. Whitespace, field order and locale choices will fork
  consensus. Use `fcl::raw::pack` over a described DTO.
- Do not verify a signature against bytes reconstructed differently from the
  signing path. Put the digest DTO next to the product protocol and cover it
  with golden raw bytes.
- Do not reuse AES-GCM nonce/key pairs. A repeated nonce under the same key is a
  confidentiality break, not a recoverable runtime warning.
- Do not treat plaintext emitted by `aes256_gcm_decoder` as committed before
  `finalize()` succeeds. For files, write a temporary artifact and rename only
  after tag verification.
- Do not call `abort()` on crypto failures in daemons. Return typed errors or
  throw std-compatible exceptions with redacted context so shutdown and
  diagnostics still run.

## Typical Mistakes

- Do not weaken canonical signature checks to make tests pass.
- Do not introduce another TLS backend through crypto dependencies.
- Do not put certificate issuance or identity enrollment product flows in
  `fcl_crypto`; this library provides primitives, not workflows.
- Do not copy vector examples into product protocols without a named DTO and
  `BOOST_DESCRIBE_STRUCT` order review.

## Tests

`test_fcl_crypto` covers hash vectors, base64/base58/hex, random bytes,
HKDF/scrypt, AES-256-GCM roundtrip/authentication failure, secp256k1/P-256
signing and recovery, WebAuthn canonical checks, BLS serialization/verification,
modular arithmetic and BLAKE2 vectors.
