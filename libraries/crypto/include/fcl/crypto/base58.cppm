module;
#include <fcl/exceptions/macros.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fcl.crypto.base58;

import fcl.core.string;
import fcl.core.utility;
export import fcl.exceptions;
import fcl.crypto.types;

export namespace fcl::crypto::base58::exceptions {

enum class code : std::uint16_t {
   invalid_character = 1,
   backend_error = 2,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto.base58")

using invalid_character = fcl::exceptions::coded_exception<code, code::invalid_character>;
using backend_error = fcl::exceptions::coded_exception<code, code::backend_error>;

} // namespace fcl::crypto::base58::exceptions

export namespace fcl::crypto {

[[nodiscard]] std::string base58_encode(std::span<const std::uint8_t> data,
                                         const fcl::yield_function_t& yield = {});
[[nodiscard]] bytes base58_decode(std::string_view base58_str);

std::string to_base58(const char* d, size_t s, const fcl::yield_function_t& yield);
std::string to_base58(const std::vector<char>& data, const fcl::yield_function_t& yield);
std::vector<char> from_base58(const std::string& base58_str);
size_t from_base58(const std::string& base58_str, char* out_data, size_t out_data_len);
} // namespace fcl::crypto
