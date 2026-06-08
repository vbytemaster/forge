module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcl/exceptions/macros.hpp>

export module fcl.transport.exceptions;

export import fcl.exceptions;

export namespace fcl::transport::exceptions {

enum class code : std::uint16_t {
   invalid_endpoint = 1,
   closed = 2,
   canceled = 3,
   frame_too_large = 4,
   protocol_error = 5,
   unsupported_protocol = 6,
   duplicate_registration = 7,
   invalid_buffer = 8,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.transport")

using invalid_endpoint = fcl::exceptions::coded_exception<code, code::invalid_endpoint>;
using closed = fcl::exceptions::coded_exception<code, code::closed>;
using canceled = fcl::exceptions::coded_exception<code, code::canceled>;
using frame_too_large = fcl::exceptions::coded_exception<code, code::frame_too_large>;
using protocol_error = fcl::exceptions::coded_exception<code, code::protocol_error>;
using unsupported_protocol = fcl::exceptions::coded_exception<code, code::unsupported_protocol>;
using duplicate_registration = fcl::exceptions::coded_exception<code, code::duplicate_registration>;
using invalid_buffer = fcl::exceptions::coded_exception<code, code::invalid_buffer>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "fcl.transport") {
      return std::nullopt;
   }
   if (actual == fcl::exceptions::make_error_code(code::invalid_endpoint)) {
      return code::invalid_endpoint;
   }
   if (actual == fcl::exceptions::make_error_code(code::closed)) {
      return code::closed;
   }
   if (actual == fcl::exceptions::make_error_code(code::canceled)) {
      return code::canceled;
   }
   if (actual == fcl::exceptions::make_error_code(code::frame_too_large)) {
      return code::frame_too_large;
   }
   if (actual == fcl::exceptions::make_error_code(code::protocol_error)) {
      return code::protocol_error;
   }
   if (actual == fcl::exceptions::make_error_code(code::unsupported_protocol)) {
      return code::unsupported_protocol;
   }
   if (actual == fcl::exceptions::make_error_code(code::duplicate_registration)) {
      return code::duplicate_registration;
   }
   if (actual == fcl::exceptions::make_error_code(code::invalid_buffer)) {
      return code::invalid_buffer;
   }
   return std::nullopt;
}

[[nodiscard]] inline bool is(const fcl::exceptions::base& value, code expected) noexcept {
   return value.code() == fcl::exceptions::make_error_code(expected);
}

} // namespace fcl::transport::exceptions
