module;
#include <forge/exceptions/macros.hpp>
#include <array>
#include <cstdint>
#include <span>

export module forge.crypto.chacha20_poly1305;

export import forge.exceptions;
import forge.crypto.types;

export namespace forge::crypto::chacha20_poly1305 {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_tag = 1,
   authentication_failed = 2,
   backend_error = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.chacha20_poly1305")

using invalid_tag = forge::exceptions::coded_exception<code, code::invalid_tag>;
using authentication_failed = forge::exceptions::coded_exception<code, code::authentication_failed>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace exceptions

using key = std::array<std::uint8_t, 32>;
using nonce = std::array<std::uint8_t, 12>;

[[nodiscard]] bytes encrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
                            std::span<const std::uint8_t> plaintext);

[[nodiscard]] bytes decrypt(const key& key, const nonce& nonce, std::span<const std::uint8_t> associated_data,
                            std::span<const std::uint8_t> ciphertext_and_tag);

} // namespace forge::crypto::chacha20_poly1305
