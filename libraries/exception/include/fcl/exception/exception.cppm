module;
#include <exception>
#include <functional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.exception.exception;

export namespace fcl::exception {

struct field {
   std::string key;
   std::string value;
   bool redacted = false;
};

field ctx(std::string_view key, std::string value);
field ctx(std::string_view key, std::string_view value);
field ctx(std::string_view key, const char* value);
field ctx(std::string_view key, bool value);
field ctx(std::string_view key, char value);
field ctx(std::string_view key, signed char value);
field ctx(std::string_view key, unsigned char value);
field ctx(std::string_view key, short value);
field ctx(std::string_view key, unsigned short value);
field ctx(std::string_view key, int value);
field ctx(std::string_view key, unsigned value);
field ctx(std::string_view key, long value);
field ctx(std::string_view key, unsigned long value);
field ctx(std::string_view key, long long value);
field ctx(std::string_view key, unsigned long long value);
field ctx(std::string_view key, float value);
field ctx(std::string_view key, double value);
field ctx(std::string_view key, long double value);

template <typename T> field ctx(std::string_view key, const T& value) {
   return ctx(key, std::string{"<unprintable>"});
}

field secret(std::string_view key, std::string value);
field secret(std::string_view key, std::string_view value);
field secret(std::string_view key, const char* value);

template <typename T> field secret(std::string_view key, const T& value) {
   auto result = ctx(key, value);
   result.redacted = true;
   result.value = "<redacted>";
   return result;
}

using fields = std::vector<field>;

class category final : public std::error_category {
 public:
   explicit category(const char* name) noexcept;

   const char* name() const noexcept override;
   std::string message(int value) const override;

 private:
   const char* _name = "";
};

struct frame {
   std::string message;
   fields context;
   std::source_location location;
};

class base : public std::runtime_error {
 public:
   base(std::string message, fields context = {},
        std::source_location location = std::source_location::current(), std::error_code code = {});

   const char* what() const noexcept override;
   const std::string& message() const noexcept;
   const fields& context() const noexcept;
   const std::source_location& location() const noexcept;
   const std::error_code& code() const noexcept;
   const std::vector<frame>& context_frames() const noexcept;

   void append_context(std::string message, fields context = {},
                       std::source_location location = std::source_location::current());

 private:
   void refresh_what();

   std::string _message;
   fields _context;
   std::source_location _location;
   std::error_code _code;
   std::vector<frame> _frames;
   std::string _what;
};

class context_error : public base {
 public:
   using base::base;
};

namespace detail {
template <typename Enum>
std::error_code enum_error_code(Enum value) {
   return make_error_code(value);
}
} // namespace detail

template <typename Enum>
std::error_code make_error_code(Enum value) {
   return detail::enum_error_code(value);
}

template <typename Enum, Enum Value>
class coded_exception : public context_error {
 public:
   using enum_type = Enum;
   static constexpr Enum value = Value;

   coded_exception(std::string message, fields context = {},
                   std::source_location location = std::source_location::current())
       : context_error(std::move(message), std::move(context), location, detail::enum_error_code(Value)) {}
};

std::string format_fields(const fields& context);
std::string format_context_message(std::string_view message, const fields& context,
                                   const std::source_location& location, const std::error_code& code = {});
std::string format_exception_chain(const std::exception& exception);
std::string format_current_exception();

using log_sink = std::function<void(std::string_view)>;
void set_log_sink(log_sink sink);
void log_current_exception(std::string_view message = {}, fields context = {});

template <typename T> void append_context(fields& out, T&& value) {
   static_assert(std::is_same_v<std::remove_cvref_t<T>, field>,
                 "FCL error context arguments must be fcl::error::field values; use fcl::error::ctx(...) or "
                 "fcl::error::secret(...).");
   out.push_back(std::forward<T>(value));
}

template <typename... Args> fields make_fields(Args&&... args) {
   fields out;
   out.reserve(sizeof...(Args));
   (append_context(out, std::forward<Args>(args)), ...);
   return out;
}

[[noreturn]] void throw_context_error(std::string_view message, fields context,
                                      std::source_location location = std::source_location::current(),
                                      std::error_code code = {});

[[noreturn]] void throw_assertion_error(std::string_view expression, std::string_view message, fields context,
                                        std::source_location location = std::source_location::current());

[[noreturn]] void throw_timeout_error(std::string_view message, fields context,
                                      std::source_location location = std::source_location::current());

[[noreturn]] void rethrow_with_context(std::string_view message, fields context,
                                       std::source_location location = std::source_location::current());

template <typename... Args>
[[noreturn]] void throw_with_context(std::string_view message, std::source_location location, Args&&... args) {
   throw_context_error(message, make_fields(std::forward<Args>(args)...), location);
}

template <typename... Args>
[[noreturn]] void throw_assertion_failure(std::string_view expression, std::source_location location, Args&&... args) {
   std::string message;
   fields context;
   context.reserve(sizeof...(Args));

   auto append_assert_arg = [&](auto&& value) {
      using value_type = std::remove_cvref_t<decltype(value)>;
      if constexpr (std::is_same_v<value_type, field>) {
         context.push_back(std::forward<decltype(value)>(value));
      } else if constexpr (std::is_convertible_v<decltype(value), std::string_view>) {
         if (message.empty()) {
            message = std::string(std::string_view(value));
         }
      }
   };

   (append_assert_arg(std::forward<Args>(args)), ...);
   throw_assertion_error(expression, message, std::move(context), location);
}

template <typename... Args>
[[noreturn]] void throw_deadline_exceeded(std::string_view message, std::source_location location, Args&&... args) {
   throw_timeout_error(message, make_fields(std::forward<Args>(args)...), location);
}

template <typename... Args>
[[noreturn]] void capture_and_rethrow(std::string_view message, std::source_location location, Args&&... args) {
   rethrow_with_context(message, make_fields(std::forward<Args>(args)...), location);
}

template <typename... Args> void capture_and_log(std::string_view message, Args&&... args) {
   log_current_exception(message, make_fields(std::forward<Args>(args)...));
}

} // namespace fcl::exception

export namespace fcl {
namespace error {
using fcl::exception::base;
using fcl::exception::capture_and_log;
using fcl::exception::capture_and_rethrow;
using fcl::exception::category;
using fcl::exception::coded_exception;
using fcl::exception::context_error;
using fcl::exception::ctx;
using fcl::exception::field;
using fcl::exception::fields;
using fcl::exception::format_context_message;
using fcl::exception::format_current_exception;
using fcl::exception::format_exception_chain;
using fcl::exception::format_fields;
using fcl::exception::frame;
using fcl::exception::log_current_exception;
using fcl::exception::log_sink;
using fcl::exception::make_error_code;
using fcl::exception::make_fields;
using fcl::exception::secret;
using fcl::exception::set_log_sink;
using fcl::exception::throw_assertion_error;
using fcl::exception::throw_assertion_failure;
using fcl::exception::throw_context_error;
using fcl::exception::throw_deadline_exceeded;
using fcl::exception::throw_timeout_error;
using fcl::exception::throw_with_context;
} // namespace error
} // namespace fcl
