module;

#include <cstdint>
#include <fcl/exceptions/macros.hpp>

export module fcl.asio.exceptions;

export import fcl.exceptions;

export namespace fcl::asio::exceptions {

enum class code : std::uint16_t {
   invalid_state = 1,
   invalid_options = 2,
   canceled = 3,
   rejected = 4,
   internal = 5,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.asio")

using invalid_state = fcl::exceptions::coded_exception<code, code::invalid_state>;
using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using canceled = fcl::exceptions::coded_exception<code, code::canceled>;
using rejected = fcl::exceptions::coded_exception<code, code::rejected>;
using internal = fcl::exceptions::coded_exception<code, code::internal>;

} // namespace fcl::asio::exceptions
