module;
#include <cstdint>
#include <variant>
#include <vector>

export module fcl.crypto.blake2;

import fcl.core.utility;
export import fcl.crypto.types;

export namespace fcl::crypto {

enum class blake2b_error : std::int32_t { input_len_error };

std::variant<blake2b_error, bytes> blake2b(std::uint32_t rounds, const bytes& h, const bytes& m, const bytes& t0_offset,
                                           const bytes& t1_offset, bool final_block, const yield_function_t& yield);

} // namespace fcl::crypto
