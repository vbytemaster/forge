module;
#include <exception>
#include <iostream>
#include <mutex>
#include <source_location>
#include <sstream>
#include <system_error>
#include <utility>

module fcl.exception.exception;

namespace {

std::mutex& sink_mutex() {
   static std::mutex mutex;
   return mutex;
}

fcl::exception::log_sink& sink_ref() {
   static fcl::exception::log_sink sink;
   return sink;
}

std::string sanitize_value(std::string_view value, bool redacted) {
   if (redacted) {
      return "<redacted>";
   }
   return std::string(value);
}

void append_exception_chain(std::ostream& out, const std::exception& exception, int depth) {
   for (int i = 0; i < depth; ++i) {
      out << "caused by: ";
   }

   if (const auto* context = dynamic_cast<const fcl::exception::base*>(&exception)) {
      out << context->what();
   } else {
      out << exception.what();
   }

   try {
      std::rethrow_if_nested(exception);
   } catch (const std::exception& nested) {
      out << '\n';
      append_exception_chain(out, nested, depth + 1);
   } catch (...) {
      out << "\n";
      for (int i = 0; i <= depth; ++i) {
         out << "caused by: ";
      }
      out << "<unknown non-std exception>";
   }
}

} // namespace

namespace fcl::exception {

field ctx(std::string_view key, std::string value) {
   return field{std::string(key), std::move(value), false};
}

field ctx(std::string_view key, std::string_view value) {
   return ctx(key, std::string(value));
}

field ctx(std::string_view key, const char* value) {
   return ctx(key, value ? std::string(value) : std::string("<null>"));
}

field ctx(std::string_view key, bool value) {
   return ctx(key, value ? "true" : "false");
}
field ctx(std::string_view key, char value) {
   return ctx(key, std::string(1, value));
}
field ctx(std::string_view key, signed char value) {
   return ctx(key, static_cast<long long>(value));
}
field ctx(std::string_view key, unsigned char value) {
   return ctx(key, static_cast<unsigned long long>(value));
}
field ctx(std::string_view key, short value) {
   return ctx(key, static_cast<long long>(value));
}
field ctx(std::string_view key, unsigned short value) {
   return ctx(key, static_cast<unsigned long long>(value));
}
field ctx(std::string_view key, int value) {
   return ctx(key, static_cast<long long>(value));
}
field ctx(std::string_view key, unsigned value) {
   return ctx(key, static_cast<unsigned long long>(value));
}
field ctx(std::string_view key, long value) {
   return ctx(key, static_cast<long long>(value));
}
field ctx(std::string_view key, unsigned long value) {
   return ctx(key, static_cast<unsigned long long>(value));
}
field ctx(std::string_view key, long long value) {
   return ctx(key, std::to_string(value));
}
field ctx(std::string_view key, unsigned long long value) {
   return ctx(key, std::to_string(value));
}
field ctx(std::string_view key, float value) {
   return ctx(key, std::to_string(value));
}
field ctx(std::string_view key, double value) {
   return ctx(key, std::to_string(value));
}
field ctx(std::string_view key, long double value) {
   return ctx(key, std::to_string(static_cast<double>(value)));
}

field secret(std::string_view key, std::string value) {
   static_cast<void>(value);
   return field{std::string(key), "<redacted>", true};
}

field secret(std::string_view key, std::string_view value) {
   return secret(key, std::string(value));
}

field secret(std::string_view key, const char* value) {
   return secret(key, value ? std::string(value) : std::string{});
}

category::category(const char* name) noexcept : _name(name == nullptr ? "" : name) {}

const char* category::name() const noexcept {
   return _name;
}

std::string category::message(int value) const {
   return std::to_string(value);
}

std::string format_fields(const fields& context) {
   if (context.empty()) {
      return {};
   }

   std::ostringstream out;
   bool first = true;
   for (const auto& item : context) {
      if (!first) {
         out << ", ";
      }
      first = false;
      out << item.key << '=' << sanitize_value(item.value, item.redacted);
   }
   return out.str();
}

std::string format_context_message(std::string_view message, const fields& context,
                                   const std::source_location& location, const std::error_code& code) {
   std::ostringstream out;
   out << message;

   if (code) {
      out << " [" << code.category().name() << ':' << code.value() << " " << code.message() << ']';
   }

   const auto formatted_fields = format_fields(context);
   if (!formatted_fields.empty()) {
      out << " {" << formatted_fields << '}';
   }

   if (location.file_name() && location.file_name()[0] != '\0') {
      out << " at " << location.file_name() << ':' << location.line();
   }

   return out.str();
}

base::base(std::string message, fields context, std::source_location location, std::error_code code)
    : std::runtime_error(std::string{}), _message(std::move(message)), _context(std::move(context)),
      _location(location), _code(std::move(code)) {
   refresh_what();
}

const char* base::what() const noexcept {
   return _what.c_str();
}

const std::string& base::message() const noexcept {
   return _message;
}

const fields& base::context() const noexcept {
   return _context;
}

const std::source_location& base::location() const noexcept {
   return _location;
}

const std::error_code& base::code() const noexcept {
   return _code;
}

const std::vector<frame>& base::context_frames() const noexcept {
   return _frames;
}

void base::append_context(std::string message, fields context, std::source_location location) {
   _frames.push_back(frame{std::move(message), std::move(context), location});
   refresh_what();
}

void base::refresh_what() {
   std::ostringstream out;
   out << format_context_message(_message, _context, _location, _code);
   for (const auto& item : _frames) {
      out << "\ncontext: " << format_context_message(item.message, item.context, item.location);
   }
   _what = out.str();
}

std::string format_exception_chain(const std::exception& exception) {
   std::ostringstream out;
   append_exception_chain(out, exception, 0);
   return out.str();
}

std::string format_current_exception() {
   try {
      throw;
   } catch (const std::exception& exception) {
      return format_exception_chain(exception);
   } catch (...) {
      return "<unknown non-std exception>";
   }
}

void set_log_sink(log_sink sink) {
   std::lock_guard lock(sink_mutex());
   sink_ref() = std::move(sink);
}

void log_current_exception(std::string_view message, fields context) {
   std::ostringstream out;
   if (!message.empty()) {
      out << message;
      const auto formatted_fields = format_fields(context);
      if (!formatted_fields.empty()) {
         out << " {" << formatted_fields << '}';
      }
      out << ": ";
   }
   out << format_current_exception();

   std::lock_guard lock(sink_mutex());
   if (sink_ref()) {
      sink_ref()(out.str());
   } else {
      std::cerr << out.str() << '\n';
   }
}

void throw_context_error(std::string_view message, fields context, std::source_location location,
                         std::error_code code) {
   throw context_error(std::string(message), std::move(context), location, std::move(code));
}

void throw_assertion_error(std::string_view expression, std::string_view message, fields context,
                           std::source_location location) {
   context.insert(context.begin(), ctx("expression", expression));
   if (message.empty()) {
      throw context_error("assertion failed", std::move(context), location,
                          std::make_error_code(std::errc::invalid_argument));
   }
   throw context_error(std::string(message), std::move(context), location,
                       std::make_error_code(std::errc::invalid_argument));
}

void throw_timeout_error(std::string_view message, fields context, std::source_location location) {
   throw context_error(message.empty() ? "deadline exceeded" : std::string(message), std::move(context), location,
                       std::make_error_code(std::errc::timed_out));
}

void rethrow_with_context(std::string_view message, fields context, std::source_location location) {
   try {
      throw;
   } catch (base& error) {
      error.append_context(std::string(message), std::move(context), location);
      throw;
   } catch (...) {
      std::throw_with_nested(context_error(std::string(message), std::move(context), location));
   }
}

} // namespace fcl::exception
