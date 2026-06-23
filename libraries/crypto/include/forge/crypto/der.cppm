module;

#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <span>

export module forge.crypto.der;

import forge.crypto.asymmetric;
import forge.crypto.types;
export import forge.exceptions;

export namespace forge::crypto::der {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_options = 2,
   backend_error = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.der")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace exceptions

[[nodiscard]] asymmetric::public_key read_public_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] asymmetric::private_key read_private_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] bytes write_public_key(const asymmetric::public_key& key);

} // namespace forge::crypto::der
