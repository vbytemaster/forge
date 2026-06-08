module;
#include <fcl/exceptions/macros.hpp>
#include <cstdint>

export module fcl.crypto.modular_arithmetic;

export import fcl.exceptions;
import fcl.crypto.types;

export namespace fcl::crypto::modular_arithmetic::exceptions {

enum class code : std::uint16_t {
   invalid_modulus = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.modular_arithmetic")

using invalid_modulus = fcl::exceptions::coded_exception<code, code::invalid_modulus>;

} // namespace fcl::crypto::modular_arithmetic::exceptions

export namespace fcl::crypto {

bytes modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus);
} // namespace fcl::crypto
