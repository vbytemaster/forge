module;

#include <fcl/exceptions/macros.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

module fcl.plugins.crypto.secrets.types;

import fcl.crypto.aes;
import fcl.crypto.kdf;
import fcl.crypto.random;
import fcl.crypto.types;
import fcl.exceptions;
import fcl.plugins.crypto.secrets.exceptions;

namespace fcl::plugins::crypto::secrets {
namespace {

constexpr auto magic = std::array<std::uint8_t, 8>{'F', 'C', 'L', 'S', 'E', 'C', '1', 0};

[[noreturn]] void throw_invalid_encrypted_secret_file();

void append_u64(fcl::crypto::bytes& out, std::uint64_t value) {
   for (auto i = 0U; i < 8U; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
   }
}

[[nodiscard]] std::uint64_t read_u64(const fcl::crypto::bytes& input, std::size_t& offset) {
   if (input.size() - offset < 8U) {
      throw_invalid_encrypted_secret_file();
   }
   auto value = std::uint64_t{0};
   for (auto i = 0U; i < 8U; ++i) {
      value |= static_cast<std::uint64_t>(input[offset++]) << (i * 8U);
   }
   return value;
}

void append_bytes(fcl::crypto::bytes& out, std::span<const std::uint8_t> value) {
   out.insert(out.end(), value.begin(), value.end());
}

[[nodiscard]] fcl::crypto::bytes read_bytes(const fcl::crypto::bytes& input, std::size_t& offset, std::uint64_t size) {
   if (size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) || input.size() - offset < size) {
      throw_invalid_encrypted_secret_file();
   }
   auto output = fcl::crypto::bytes{input.begin() + static_cast<std::ptrdiff_t>(offset),
                                    input.begin() + static_cast<std::ptrdiff_t>(offset + size)};
   offset += static_cast<std::size_t>(size);
   return output;
}

[[nodiscard]] fcl::crypto::aes256_key derive_file_key(const std::string& passphrase,
                                                       const fcl::crypto::bytes& salt,
                                                       std::uint64_t n,
                                                       std::uint64_t r,
                                                       std::uint64_t p,
                                                       std::uint64_t max_memory_bytes) {
   return fcl::crypto::make_aes256_key(fcl::crypto::derive_scrypt({
      .password = passphrase,
      .salt = salt,
      .n = n,
      .r = r,
      .p = p,
      .max_memory_bytes = max_memory_bytes,
      .output_size = fcl::crypto::aes256_key_size,
   }));
}

void validate_scrypt_limit(std::string_view name, std::uint64_t value, std::uint64_t max_value) {
   if (value == 0 || value > max_value) {
      FCL_THROW_EXCEPTION(exceptions::invalid_secret, "encrypted secret file scrypt parameter is outside configured limit",
                          fcl::exceptions::ctx("parameter", name),
                          fcl::exceptions::ctx("value", value),
                          fcl::exceptions::ctx("max", max_value));
   }
}

void validate_scrypt_limits(std::uint64_t n,
                            std::uint64_t r,
                            std::uint64_t p,
                            std::uint64_t max_memory_bytes,
                            encrypted_file_decrypt_limits limits) {
   validate_scrypt_limit("n", n, limits.max_scrypt_n);
   validate_scrypt_limit("r", r, limits.max_scrypt_r);
   validate_scrypt_limit("p", p, limits.max_scrypt_p);
   validate_scrypt_limit("max_memory_bytes", max_memory_bytes, limits.max_scrypt_memory_bytes);
}

[[noreturn]] void throw_invalid_encrypted_secret_file() {
   FCL_THROW_EXCEPTION(exceptions::invalid_secret, "encrypted secret file cannot be decrypted");
}

[[noreturn]] void throw_invalid_encrypted_secret_file_parameter(
   const fcl::exceptions::runtime_coded_exception<fcl::crypto::aes::exceptions::code>& error) {
   switch (error.value()) {
   case fcl::crypto::aes::exceptions::code::invalid_nonce:
   case fcl::crypto::aes::exceptions::code::invalid_tag:
   case fcl::crypto::aes::exceptions::code::authentication_failed:
      throw_invalid_encrypted_secret_file();
   default:
      throw;
   }
}

[[noreturn]] void throw_invalid_encrypted_secret_file_parameter(
   const fcl::exceptions::runtime_coded_exception<fcl::crypto::kdf::exceptions::code>& error) {
   switch (error.value()) {
   case fcl::crypto::kdf::exceptions::code::invalid_options:
   case fcl::crypto::kdf::exceptions::code::backend_error:
      throw_invalid_encrypted_secret_file();
   default:
      throw;
   }
}

[[nodiscard]] fcl::crypto::bytes make_header(std::uint64_t n,
                                             std::uint64_t r,
                                             std::uint64_t p,
                                             std::uint64_t max_memory_bytes,
                                             const fcl::crypto::bytes& salt,
                                             const fcl::crypto::bytes& nonce,
                                             std::uint64_t ciphertext_size) {
   auto header = fcl::crypto::bytes{};
   append_bytes(header, magic);
   append_u64(header, n);
   append_u64(header, r);
   append_u64(header, p);
   append_u64(header, max_memory_bytes);
   append_u64(header, salt.size());
   append_u64(header, nonce.size());
   append_u64(header, ciphertext_size);
   append_bytes(header, salt);
   append_bytes(header, nonce);
   return header;
}

} // namespace

fcl::crypto::bytes encrypt_secret_file(encrypted_file_encrypt_request request) {
   if (request.passphrase.empty() || request.plaintext.empty()) {
      FCL_THROW("encrypted secret file requires passphrase and plaintext");
   }
   if (request.salt.empty()) {
      request.salt = fcl::crypto::random_bytes(16);
   }
   if (request.nonce.empty()) {
      request.nonce = fcl::crypto::random_bytes(fcl::crypto::aes_gcm_nonce_size);
   }

   const auto header = make_header(request.scrypt_n,
                                   request.scrypt_r,
                                   request.scrypt_p,
                                   request.scrypt_max_memory_bytes,
                                   request.salt,
                                   request.nonce,
                                   request.plaintext.size());
   auto encrypted = fcl::crypto::encrypt_aes256_gcm({
      .key = derive_file_key(request.passphrase,
                             request.salt,
                             request.scrypt_n,
                             request.scrypt_r,
                             request.scrypt_p,
                             request.scrypt_max_memory_bytes),
      .nonce = request.nonce,
      .plaintext = std::move(request.plaintext),
      .aad = header,
   });

   auto output = header;
   append_bytes(output, encrypted.tag);
   append_bytes(output, encrypted.ciphertext);
   return output;
}

fcl::crypto::bytes decrypt_secret_file(const fcl::crypto::bytes& container,
                                       const std::string& passphrase,
                                       encrypted_file_decrypt_limits limits) {
   if (container.size() < magic.size() || !std::equal(magic.begin(), magic.end(), container.begin())) {
      throw_invalid_encrypted_secret_file();
   }
   auto offset = magic.size();
   const auto n = read_u64(container, offset);
   const auto r = read_u64(container, offset);
   const auto p = read_u64(container, offset);
   const auto max_memory_bytes = read_u64(container, offset);
   const auto salt_size = read_u64(container, offset);
   const auto nonce_size = read_u64(container, offset);
   const auto ciphertext_size = read_u64(container, offset);
   validate_scrypt_limits(n, r, p, max_memory_bytes, limits);
   if (ciphertext_size > limits.max_plaintext_bytes) {
      FCL_THROW_EXCEPTION(exceptions::size_limit_exceeded, "encrypted secret file plaintext exceeds configured limit",
                          fcl::exceptions::ctx("size", ciphertext_size),
                          fcl::exceptions::ctx("max", limits.max_plaintext_bytes));
   }
   auto salt = read_bytes(container, offset, salt_size);
   auto nonce = read_bytes(container, offset, nonce_size);
   auto tag = read_bytes(container, offset, fcl::crypto::aes_gcm_tag_size);
   auto ciphertext = read_bytes(container, offset, ciphertext_size);
   if (offset != container.size()) {
      throw_invalid_encrypted_secret_file();
   }
   auto header = make_header(n, r, p, max_memory_bytes, salt, nonce, ciphertext_size);
   try {
      return fcl::crypto::decrypt_aes256_gcm({
         .key = derive_file_key(passphrase, salt, n, r, p, max_memory_bytes),
         .encrypted =
            fcl::crypto::aes256_gcm_ciphertext{
               .nonce = std::move(nonce),
               .tag = std::move(tag),
               .ciphertext = std::move(ciphertext),
            },
         .aad = std::move(header),
      });
   } catch (const fcl::crypto::aes::exceptions::invalid_nonce&) {
      throw_invalid_encrypted_secret_file();
   } catch (const fcl::crypto::aes::exceptions::invalid_tag&) {
      throw_invalid_encrypted_secret_file();
   } catch (const fcl::crypto::aes::exceptions::authentication_failed&) {
      throw_invalid_encrypted_secret_file();
   } catch (const fcl::crypto::kdf::exceptions::invalid_options&) {
      throw_invalid_encrypted_secret_file();
   } catch (const fcl::crypto::kdf::exceptions::backend_error&) {
      throw_invalid_encrypted_secret_file();
   } catch (const fcl::exceptions::runtime_coded_exception<fcl::crypto::aes::exceptions::code>& error) {
      throw_invalid_encrypted_secret_file_parameter(error);
   } catch (const fcl::exceptions::runtime_coded_exception<fcl::crypto::kdf::exceptions::code>& error) {
      throw_invalid_encrypted_secret_file_parameter(error);
   }
}

} // namespace fcl::plugins::crypto::secrets
