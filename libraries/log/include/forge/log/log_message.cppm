module;
#include <forge/exceptions/macros.hpp>
#include <boost/describe.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

export module forge.log.log_message;

import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

/**
 * @file log_message.hpp
 * @brief Defines types and helper macros necessary for generating log messages.
 */

export namespace forge {
void set_thread_name(const std::string& name);
const std::string& get_thread_name();

namespace detail {
class log_context_impl;
class log_message_impl;
} // namespace detail

/**
 * Named scope for log_level enumeration.
 */
class log_level {
 public:
   /**
    * @brief Define's the various log levels for reporting.
    *
    * Each log level includes all higher levels such that
    * Debug includes Error, but Error does not include Debug.
    */
   enum values { all, debug, info, warn, error, off };
   BOOST_DESCRIBE_NESTED_ENUM(values, all, debug, info, warn, error, off)
   log_level(values v = off) : value(v) {}
   explicit log_level(int v) : value(static_cast<values>(v)) {}
   operator int() const {
      return value;
   }
   std::string to_string() const;
   values value;
};

void to_variant(log_level e, variant& v);
void from_variant(const variant& e, log_level& ll);

/**
 *  @brief provides information about where and when a log message was generated.
 *  @ingroup AthenaSerializable
 *
 *  @see FORGE_LOG_CONTEXT
 */
class log_context {
 public:
   log_context();
   log_context(log_level ll, const char* file, uint64_t line, const char* method);
   ~log_context();
   explicit log_context(const variant& v);
   variant to_variant() const;

   std::string get_file() const;
   uint64_t get_line_number() const;
   std::string get_method() const;
   std::string get_thread_name() const;
   std::string get_task_name() const;
   std::string get_host_name() const;
   std::chrono::sys_time<std::chrono::microseconds> get_timestamp() const;
   log_level get_log_level() const;
   std::string get_context() const;

   void append_context(const std::string& c);

   std::string to_string() const;

 private:
   std::shared_ptr<detail::log_context_impl> my;
};

void to_variant(const log_context& l, variant& v);
void from_variant(const variant& l, log_context& c);

/**
 *  @brief aggregates a message along with the context and associated meta-information.
 *  @ingroup AthenaSerializable
 *
 *  @note log_message has reference semantics, all copies refer to the same log message
 *  and the message is read-only after construction.
 *
 *  When converted to JSON, log_message has the following form:
 *  @code
 *  {
 *     "context" : { ... },
 *     "format"  : "string with ${keys}",
 *     "data"    : { "keys" : "values" }
 *  }
 *  @endcode
 *
 *  @see FORGE_LOG_MESSAGE
 */
class log_message {
 public:
   log_message();
   /**
    *  @param ctx - generally provided using the FORGE_LOG_CONTEXT(LEVEL) macro
    */
   log_message(log_context ctx, std::string format, variant_object args = variant_object());
   ~log_message();

   log_message(const variant& v);
   variant to_variant() const;

   std::string get_message() const;
   /**
    * A faster version of get_message which does limited formatting and excludes large variants
    * @return formatted message according to format and variant args
    */
   std::string get_limited_message() const;

   log_context get_context() const;
   std::string get_format() const;
   variant_object get_data() const;

 private:
   std::shared_ptr<detail::log_message_impl> my;
};

void to_variant(const log_message& l, variant& v);
void from_variant(const variant& l, log_message& c);

typedef std::vector<log_message> log_messages;

} // namespace forge

export namespace forge {
BOOST_DESCRIBE_STRUCT(log_level, (), (value))
}
