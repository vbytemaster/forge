module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <fcl/exceptions/macros.hpp>

export module fcl.stcp.exceptions;

export import fcl.exceptions;

export namespace fcl::stcp::exceptions {

enum class code : std::uint16_t {
   invalid_endpoint = 1,
   invalid_options = 2,
   connect_failed = 3,
   listen_failed = 4,
   accept_failed = 5,
   handshake_failed = 6,
   verification_failed = 7,
   closed = 8,
   canceled = 9,
   io_error = 10,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.stcp")

using invalid_endpoint = fcl::exceptions::coded_exception<code, code::invalid_endpoint>;
using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using connect_failed = fcl::exceptions::coded_exception<code, code::connect_failed>;
using listen_failed = fcl::exceptions::coded_exception<code, code::listen_failed>;
using accept_failed = fcl::exceptions::coded_exception<code, code::accept_failed>;
using handshake_failed = fcl::exceptions::coded_exception<code, code::handshake_failed>;
using verification_failed = fcl::exceptions::coded_exception<code, code::verification_failed>;
using closed = fcl::exceptions::coded_exception<code, code::closed>;
using canceled = fcl::exceptions::coded_exception<code, code::canceled>;
using io_error = fcl::exceptions::coded_exception<code, code::io_error>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "fcl.stcp") {
      return std::nullopt;
   }
   if (actual == fcl::exceptions::make_error_code(code::invalid_endpoint)) {
      return code::invalid_endpoint;
   }
   if (actual == fcl::exceptions::make_error_code(code::invalid_options)) {
      return code::invalid_options;
   }
   if (actual == fcl::exceptions::make_error_code(code::connect_failed)) {
      return code::connect_failed;
   }
   if (actual == fcl::exceptions::make_error_code(code::listen_failed)) {
      return code::listen_failed;
   }
   if (actual == fcl::exceptions::make_error_code(code::accept_failed)) {
      return code::accept_failed;
   }
   if (actual == fcl::exceptions::make_error_code(code::handshake_failed)) {
      return code::handshake_failed;
   }
   if (actual == fcl::exceptions::make_error_code(code::verification_failed)) {
      return code::verification_failed;
   }
   if (actual == fcl::exceptions::make_error_code(code::closed)) {
      return code::closed;
   }
   if (actual == fcl::exceptions::make_error_code(code::canceled)) {
      return code::canceled;
   }
   if (actual == fcl::exceptions::make_error_code(code::io_error)) {
      return code::io_error;
   }
   return std::nullopt;
}

[[nodiscard]] inline bool is(const fcl::exceptions::base& value, code expected) noexcept {
   return value.code() == fcl::exceptions::make_error_code(expected);
}

} // namespace fcl::stcp::exceptions
