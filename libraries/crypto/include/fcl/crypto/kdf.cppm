module;

#include <fcl/exceptions/macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

export module fcl.crypto.kdf;

export import fcl.exceptions;
import fcl.crypto.types;

export namespace fcl::crypto::kdf::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_options = 2,
   backend_error = 3,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.kdf")

using invalid_key = fcl::exceptions::coded_exception<code, code::invalid_key>;
using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using backend_error = fcl::exceptions::coded_exception<code, code::backend_error>;

} // namespace fcl::crypto::kdf::exceptions

export namespace fcl::crypto {

inline constexpr auto default_derived_key_size = std::size_t{32};

struct hkdf_sha256_request {
   bytes secret;
   bytes salt;
   bytes info;
   std::size_t output_size = default_derived_key_size;
};

struct hkdf_sha256_span_request {
   std::span<const std::uint8_t> secret;
   std::span<const std::uint8_t> salt;
   std::span<const std::uint8_t> info;
   std::size_t output_size = default_derived_key_size;
};

struct scrypt_request {
   std::string password;
   bytes salt;
   std::uint64_t n = 16'384;
   std::uint64_t r = 8;
   std::uint64_t p = 1;
   std::uint64_t max_memory_bytes = 32ULL * 1024ULL * 1024ULL;
   std::size_t output_size = default_derived_key_size;
};

[[nodiscard]] bytes derive_hkdf_sha256(const hkdf_sha256_request& request);
[[nodiscard]] bytes derive_hkdf_sha256(const hkdf_sha256_span_request& request);
[[nodiscard]] bytes derive_scrypt(const scrypt_request& request);

} // namespace fcl::crypto
