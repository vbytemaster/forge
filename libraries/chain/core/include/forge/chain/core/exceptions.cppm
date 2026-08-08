module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.core.exceptions;

export import forge.exceptions;

export namespace forge::chain::core::exceptions {

enum class code : std::uint16_t {
   invalid_leaf_index = 1,
   leaf_count_overflow = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.core")

using invalid_leaf_index = forge::exceptions::coded_exception<code, code::invalid_leaf_index>;
using leaf_count_overflow = forge::exceptions::coded_exception<code, code::leaf_count_overflow>;

} // namespace forge::chain::core::exceptions
