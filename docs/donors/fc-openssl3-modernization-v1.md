# fc OpenSSL 3 modernization donor traceability v1

This pass is limited to `storlane-fc`. The downstream `storlane` dependency bump is intentionally out of scope until the `storlane-fc` review is accepted.

| Donor / source | Inspected files | Accepted pattern | Rejected pattern | Storlane target | Proof |
| --- | --- | --- | --- | --- | --- |
| BitStore fc | `bitstore-fc/CMakeLists.txt`, `bitstore-fc/README-ecc.md`, `bitstore-fc/src/crypto/*` | Find and link a single external OpenSSL backend; keep `secp256k1` as a separate K1 implementation; preserve compatibility through crypto tests. | Relying on broad deprecated-warning suppression as the final migration state. | `storlane-fc` CMake and crypto implementation. | `test_fc`, static grep and `otool` link proof. |
| Current storlane-fc | `CMakeLists.txt`, `src/crypto/*`, `include/fc/crypto/*`, `test/crypto/*` | Preserve existing public behavior for K1, R1, WebAuthn, Base58, hash, AES and BLS tests. | Disabling R1/WebAuthn tests to make the OpenSSL 3 migration easier. | `fc` public crypto contract remains source-compatible. | Baseline BoringSSL `test_fc` and post-migration `test_fc`. |
| OpenSSL 3 | Homebrew OpenSSL 3.6 headers and CMake package | Use `OpenSSL::Crypto`, EVP digest/cipher APIs, provider-compatible key APIs where key management is required, and `OPENSSL_NO_DEPRECATED` in the final build. | Keeping `EC_KEY`, `ECDSA_*`, `SHA*_Init/Update/Final`, BoringSSL allocation/free or mixed crypto backends. | One OpenSSL 3 `libcrypto` backend for host binaries. | No OpenSSL 3 deprecation warnings and no BoringSSL symbols in final link graph. |
| secp256k1 | `secp256k1/` vendored library and current K1 tests | Keep K1 signing/recovery on specialized `secp256k1`; OpenSSL 3 migration must not alter canonical K1 behavior. | Replacing K1 with OpenSSL ECDSA or changing recoverable signature format. | `fc::ecc` K1 path. | K1 recovery and string roundtrip tests. |
