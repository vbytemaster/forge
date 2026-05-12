module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

export module fcl.crypto.types;

export namespace fcl::crypto {

using bytes = std::vector<std::uint8_t>;

inline constexpr auto aes256_key_size = std::size_t{32};
inline constexpr auto aes_cbc_iv_size = std::size_t{16};
inline constexpr auto aes_gcm_nonce_size = std::size_t{12};
inline constexpr auto aes_gcm_tag_size = std::size_t{16};

enum class error_kind {
   invalid_key,
   invalid_nonce,
   invalid_tag,
   invalid_options,
   authentication_failed,
   backend_error,
};

class error final : public std::runtime_error {
public:
   error(error_kind kind, std::string message);

   [[nodiscard]] error_kind kind() const noexcept;

private:
   error_kind _kind;
};

struct aes256_key {
   std::array<std::uint8_t, aes256_key_size> bytes{};
};

struct aes256_gcm_ciphertext {
   bytes nonce;
   bytes tag;
   bytes ciphertext;
};

struct aes256_gcm_encrypt_request {
   aes256_key key;
   bytes nonce;
   bytes plaintext;
   bytes aad;
};

struct aes256_gcm_decrypt_request {
   aes256_key key;
   aes256_gcm_ciphertext encrypted;
   bytes aad;
};

struct aes256_cbc_ciphertext {
   bytes iv;
   bytes ciphertext;
};

struct aes256_cbc_encrypt_request {
   aes256_key key;
   bytes iv;
   bytes plaintext;
};

struct aes256_cbc_decrypt_request {
   aes256_key key;
   aes256_cbc_ciphertext encrypted;
};

struct hkdf_sha256_request {
   bytes secret;
   bytes salt;
   bytes info;
   std::size_t output_size = aes256_key_size;
};

struct scrypt_request {
   std::string password;
   bytes salt;
   std::uint64_t n = 16'384;
   std::uint64_t r = 8;
   std::uint64_t p = 1;
   std::uint64_t max_memory_bytes = 32ULL * 1024ULL * 1024ULL;
   std::size_t output_size = aes256_key_size;
};

} // namespace fcl::crypto
