module;
#include <fcl/exceptions/macros.hpp>
#include <cstdint>
#include <vector>

export module fcl.crypto.blake2;

import fcl.core.utility;
export import fcl.exceptions;
export import fcl.crypto.types;

export namespace fcl::crypto::blake2::exceptions {

enum class code : std::uint16_t {
   invalid_input = 1,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.blake2")

using invalid_input = fcl::exceptions::coded_exception<code, code::invalid_input>;

} // namespace fcl::crypto::blake2::exceptions

export namespace fcl::crypto {

bytes blake2b(std::uint32_t rounds, const bytes& h, const bytes& m, const bytes& t0_offset, const bytes& t1_offset,
              bool final_block, const yield_function_t& yield);

} // namespace fcl::crypto
