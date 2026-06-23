module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>

export module forge.crypto.modular_arithmetic;

export import forge.exceptions;
import forge.crypto.types;

export namespace forge::crypto::modular_arithmetic::exceptions {

enum class code : std::uint16_t {
   invalid_modulus = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.modular_arithmetic")

using invalid_modulus = forge::exceptions::coded_exception<code, code::invalid_modulus>;

} // namespace forge::crypto::modular_arithmetic::exceptions

export namespace forge::crypto {

bytes modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus);
} // namespace forge::crypto
