module;

#include <fcl/exceptions/macros.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <string>

module fcl.crypto.kdf;

namespace fcl::crypto {
namespace {

void require_output_size(std::size_t size) {
   if (size == 0 || size > 255U * 32U) {
      FCL_THROW_EXCEPTION(kdf::exceptions::invalid_options, "invalid KDF output size");
   }
}

int checked_int_size(std::size_t size, const char* label) {
   if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      FCL_THROW_EXCEPTION(kdf::exceptions::invalid_options, std::string(label) + " is too large");
   }
   return static_cast<int>(size);
}

[[nodiscard]] bytes hmac_sha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> input) {
   auto out = bytes(EVP_MAX_MD_SIZE);
   auto out_size = 0U;
   if (HMAC(EVP_sha256(), key.data(), checked_int_size(key.size(), "HMAC key"), input.data(), input.size(), out.data(),
            &out_size) == nullptr) {
      FCL_THROW_EXCEPTION(kdf::exceptions::backend_error, "OpenSSL HMAC-SHA256 failed");
   }
   out.resize(out_size);
   return out;
}

[[nodiscard]] bytes derive_hkdf_sha256_with_salt(std::span<const std::uint8_t> secret,
                                                 std::span<const std::uint8_t> salt,
                                                 std::span<const std::uint8_t> info,
                                                 std::size_t output_size) {
   const auto prk = hmac_sha256(salt, secret);

   auto out = bytes{};
   out.reserve(output_size);
   auto previous = bytes{};
   auto counter = std::uint8_t{1};
   while (out.size() < output_size) {
      auto input = bytes{};
      input.reserve(previous.size() + info.size() + 1U);
      input.insert(input.end(), previous.begin(), previous.end());
      input.insert(input.end(), info.begin(), info.end());
      input.push_back(counter++);
      previous = hmac_sha256(prk, input);
      const auto remaining = output_size - out.size();
      out.insert(out.end(), previous.begin(),
                 previous.begin() + static_cast<std::ptrdiff_t>(std::min(previous.size(), remaining)));
   }
   return out;
}

} // namespace

bytes derive_hkdf_sha256(const hkdf_sha256_request& request) {
   return derive_hkdf_sha256(hkdf_sha256_span_request{
      .secret = request.secret,
      .salt = request.salt,
      .info = request.info,
      .output_size = request.output_size,
   });
}

bytes derive_hkdf_sha256(const hkdf_sha256_span_request& request) {
   require_output_size(request.output_size);
   if (request.secret.empty()) {
      FCL_THROW_EXCEPTION(kdf::exceptions::invalid_key, "HKDF requires secret input");
   }

   if (request.salt.empty()) {
      const auto zero_salt = bytes(32, 0);
      return derive_hkdf_sha256_with_salt(request.secret, zero_salt, request.info, request.output_size);
   }
   return derive_hkdf_sha256_with_salt(request.secret, request.salt, request.info, request.output_size);
}

bytes derive_scrypt(const scrypt_request& request) {
   if (request.password.empty() || request.salt.empty()) {
      FCL_THROW_EXCEPTION(kdf::exceptions::invalid_options, "scrypt requires password and salt");
   }
   require_output_size(request.output_size);
   if (request.n == 0 || request.r == 0 || request.p == 0 || request.max_memory_bytes == 0) {
      FCL_THROW_EXCEPTION(kdf::exceptions::invalid_options, "invalid scrypt parameters");
   }

   auto out = bytes(request.output_size);
   if (EVP_PBE_scrypt(request.password.data(), request.password.size(), request.salt.data(), request.salt.size(),
                      request.n, request.r, request.p, request.max_memory_bytes, out.data(), out.size()) != 1) {
      FCL_THROW_EXCEPTION(kdf::exceptions::backend_error, "OpenSSL scrypt failed");
   }
   return out;
}

} // namespace fcl::crypto
