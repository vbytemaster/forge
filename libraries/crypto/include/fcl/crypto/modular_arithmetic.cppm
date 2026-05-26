module;
#include <cstdint>
#include <variant>

export module fcl.crypto.modular_arithmetic;

import fcl.crypto.types;

export namespace fcl::crypto {
enum class modular_arithmetic_error : int32_t {
   modulus_len_zero,
};

std::variant<modular_arithmetic_error, bytes> modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus);
} // namespace fcl::crypto
