module;

#include <fcl/exception/macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

export module fcl.crypto.aes;

export import fcl.exception.exception;
import fcl.crypto.types;

export namespace fcl::crypto::aes::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_nonce = 2,
   invalid_tag = 3,
   invalid_options = 4,
   authentication_failed = 5,
   backend_error = 6,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.aes")

using invalid_key = fcl::exception::coded_exception<code, code::invalid_key>;
using invalid_nonce = fcl::exception::coded_exception<code, code::invalid_nonce>;
using invalid_tag = fcl::exception::coded_exception<code, code::invalid_tag>;
using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;
using authentication_failed = fcl::exception::coded_exception<code, code::authentication_failed>;
using backend_error = fcl::exception::coded_exception<code, code::backend_error>;

} // namespace fcl::crypto::aes::exceptions

export namespace fcl::crypto {

inline constexpr auto aes256_key_size = std::size_t{32};
inline constexpr auto aes_cbc_iv_size = std::size_t{16};
inline constexpr auto aes_gcm_nonce_size = std::size_t{12};
inline constexpr auto aes_gcm_tag_size = std::size_t{16};

using aes_byte_sink = std::function<void(std::span<const std::uint8_t>)>;

struct aes256_key {
   std::array<std::uint8_t, aes256_key_size> bytes{};
};

struct aes256_gcm_authentication {
   bytes nonce;
   bytes tag;
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

struct aes256_gcm_encoder_options {
   aes256_key key;
   bytes nonce;
   bytes aad;
   aes_byte_sink ciphertext_sink;
};

struct aes256_gcm_decoder_options {
   aes256_key key;
   bytes nonce;
   bytes tag;
   bytes aad;
   aes_byte_sink plaintext_sink;
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

[[nodiscard]] aes256_key make_aes256_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] aes256_key generate_aes256_key();

class aes256_gcm_encoder {
 public:
   explicit aes256_gcm_encoder(aes256_gcm_encoder_options options);
   ~aes256_gcm_encoder();

   aes256_gcm_encoder(aes256_gcm_encoder&&) noexcept;
   aes256_gcm_encoder& operator=(aes256_gcm_encoder&&) noexcept;

   aes256_gcm_encoder(const aes256_gcm_encoder&) = delete;
   aes256_gcm_encoder& operator=(const aes256_gcm_encoder&) = delete;

   void write(const char* data, std::size_t size);
   void write(std::span<const std::uint8_t> data);

   [[nodiscard]] aes256_gcm_authentication finalize();

 private:
   struct impl;
   std::unique_ptr<impl> _impl;
};

class aes256_gcm_decoder {
 public:
   explicit aes256_gcm_decoder(aes256_gcm_decoder_options options);
   ~aes256_gcm_decoder();

   aes256_gcm_decoder(aes256_gcm_decoder&&) noexcept;
   aes256_gcm_decoder& operator=(aes256_gcm_decoder&&) noexcept;

   aes256_gcm_decoder(const aes256_gcm_decoder&) = delete;
   aes256_gcm_decoder& operator=(const aes256_gcm_decoder&) = delete;

   void write(const char* data, std::size_t size);
   void write(std::span<const std::uint8_t> data);

   void finalize();

 private:
   struct impl;
   std::unique_ptr<impl> _impl;
};

[[nodiscard]] aes256_gcm_ciphertext encrypt_aes256_gcm(const aes256_gcm_encrypt_request& request);

[[nodiscard]] bytes decrypt_aes256_gcm(const aes256_gcm_decrypt_request& request);

[[nodiscard]] aes256_cbc_ciphertext encrypt_aes256_cbc(const aes256_cbc_encrypt_request& request);

[[nodiscard]] bytes decrypt_aes256_cbc(const aes256_cbc_decrypt_request& request);

} // namespace fcl::crypto
