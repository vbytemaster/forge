module;
#include <forge/exceptions/macros.hpp>
#include <cstdint>
#include <vector>

export module forge.crypto.blake2;

import forge.core.utility;
export import forge.exceptions;
export import forge.crypto.types;

export namespace forge::crypto::blake2::exceptions {

enum class code : std::uint16_t {
   invalid_input = 1,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.blake2")

using invalid_input = forge::exceptions::coded_exception<code, code::invalid_input>;

} // namespace forge::crypto::blake2::exceptions

export namespace forge::crypto {

bytes blake2b(std::uint32_t rounds, const bytes& h, const bytes& m, const bytes& t0_offset, const bytes& t1_offset,
              bool final_block, const yield_function_t& yield);

} // namespace forge::crypto
