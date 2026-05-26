module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcl/exception/macros.hpp>

export module fcl.crypto.exceptions;

export import fcl.exception.exception;

export namespace fcl::crypto::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_nonce = 2,
   invalid_tag = 3,
   invalid_options = 4,
   authentication_failed = 5,
   backend_error = 6,
};

FCL_DECLARE_EXCEPTION_CATEGORY(code, "fcl.crypto")

using invalid_key = fcl::exception::coded_exception<code, code::invalid_key>;
using invalid_nonce = fcl::exception::coded_exception<code, code::invalid_nonce>;
using invalid_tag = fcl::exception::coded_exception<code, code::invalid_tag>;
using invalid_options = fcl::exception::coded_exception<code, code::invalid_options>;
using authentication_failed = fcl::exception::coded_exception<code, code::authentication_failed>;
using backend_error = fcl::exception::coded_exception<code, code::backend_error>;

[[nodiscard]] inline std::optional<code> code_of(const fcl::exception::base& error) noexcept {
   const auto& value = error.code();
   if (!value || std::string_view{value.category().name()} != "fcl.crypto") {
      return std::nullopt;
   }
   return static_cast<code>(value.value());
}

[[nodiscard]] inline bool is(const fcl::exception::base& error, code value) noexcept {
   return error.code() == fcl::exception::make_error_code(value);
}

[[noreturn]] inline void raise(code value, std::string message) {
   switch (value) {
   case code::invalid_key:
      throw invalid_key{std::move(message)};
   case code::invalid_nonce:
      throw invalid_nonce{std::move(message)};
   case code::invalid_tag:
      throw invalid_tag{std::move(message)};
   case code::invalid_options:
      throw invalid_options{std::move(message)};
   case code::authentication_failed:
      throw authentication_failed{std::move(message)};
   case code::backend_error:
      throw backend_error{std::move(message)};
   }
   throw backend_error{std::move(message)};
}

} // namespace fcl::crypto::exceptions
