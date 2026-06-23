module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <forge/exceptions/macros.hpp>

export module forge.stcp.exceptions;

export import forge.exceptions;

export namespace forge::stcp::exceptions {

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

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.stcp")

using invalid_endpoint = forge::exceptions::coded_exception<code, code::invalid_endpoint>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;
using connect_failed = forge::exceptions::coded_exception<code, code::connect_failed>;
using listen_failed = forge::exceptions::coded_exception<code, code::listen_failed>;
using accept_failed = forge::exceptions::coded_exception<code, code::accept_failed>;
using handshake_failed = forge::exceptions::coded_exception<code, code::handshake_failed>;
using verification_failed = forge::exceptions::coded_exception<code, code::verification_failed>;
using closed = forge::exceptions::coded_exception<code, code::closed>;
using canceled = forge::exceptions::coded_exception<code, code::canceled>;
using io_error = forge::exceptions::coded_exception<code, code::io_error>;

[[nodiscard]] inline std::optional<code> code_of(const forge::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "forge.stcp") {
      return std::nullopt;
   }
   if (actual == forge::exceptions::make_error_code(code::invalid_endpoint)) {
      return code::invalid_endpoint;
   }
   if (actual == forge::exceptions::make_error_code(code::invalid_options)) {
      return code::invalid_options;
   }
   if (actual == forge::exceptions::make_error_code(code::connect_failed)) {
      return code::connect_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::listen_failed)) {
      return code::listen_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::accept_failed)) {
      return code::accept_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::handshake_failed)) {
      return code::handshake_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::verification_failed)) {
      return code::verification_failed;
   }
   if (actual == forge::exceptions::make_error_code(code::closed)) {
      return code::closed;
   }
   if (actual == forge::exceptions::make_error_code(code::canceled)) {
      return code::canceled;
   }
   if (actual == forge::exceptions::make_error_code(code::io_error)) {
      return code::io_error;
   }
   return std::nullopt;
}

[[nodiscard]] inline bool is(const forge::exceptions::base& value, code expected) noexcept {
   return value.code() == forge::exceptions::make_error_code(expected);
}

} // namespace forge::stcp::exceptions
