module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <fcl/exceptions/macros.hpp>

export module fcl.yamux.exceptions;

export import fcl.exceptions;

export namespace fcl::yamux::exceptions {

enum class code : std::uint16_t {
   invalid_options = 1,
   protocol_error = 2,
   resource_limit = 3,
   stream_reset = 4,
   closed = 5,
   canceled = 6,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.yamux")

using invalid_options = fcl::exceptions::coded_exception<code, code::invalid_options>;
using protocol_error = fcl::exceptions::coded_exception<code, code::protocol_error>;
using resource_limit = fcl::exceptions::coded_exception<code, code::resource_limit>;
using stream_reset = fcl::exceptions::coded_exception<code, code::stream_reset>;
using closed = fcl::exceptions::coded_exception<code, code::closed>;
using canceled = fcl::exceptions::coded_exception<code, code::canceled>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exceptions::base& value) noexcept {
   const auto& actual = value.code();
   if (!actual || std::string_view{actual.category().name()} != "fcl.yamux") {
      return std::nullopt;
   }
   if (actual == fcl::exceptions::make_error_code(code::invalid_options)) {
      return code::invalid_options;
   }
   if (actual == fcl::exceptions::make_error_code(code::protocol_error)) {
      return code::protocol_error;
   }
   if (actual == fcl::exceptions::make_error_code(code::resource_limit)) {
      return code::resource_limit;
   }
   if (actual == fcl::exceptions::make_error_code(code::stream_reset)) {
      return code::stream_reset;
   }
   if (actual == fcl::exceptions::make_error_code(code::closed)) {
      return code::closed;
   }
   if (actual == fcl::exceptions::make_error_code(code::canceled)) {
      return code::canceled;
   }
   return std::nullopt;
}

[[nodiscard]] inline bool is(const fcl::exceptions::base& value, code expected) noexcept {
   return value.code() == fcl::exceptions::make_error_code(expected);
}

} // namespace fcl::yamux::exceptions
