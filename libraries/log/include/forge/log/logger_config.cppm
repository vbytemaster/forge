module;
#include <boost/describe.hpp>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>

export module forge.log.logger_config;

import forge.log.logger;
import forge.log.appender;
export import forge.log.log_message;
import forge.variant.described;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

export namespace forge {
struct appender_config {
   explicit appender_config(std::string name = {}, std::string type = {}, variant args = {})
       : name(std::move(name)), type(std::move(type)), args(std::move(args)) {}
   std::string name;
   std::string type;
   forge::variant args;
};

struct logger_config {
   explicit logger_config(std::string name = {}) : name(std::move(name)) {}
   std::string name;
   /// if not set, then parents level is used.
   std::optional<log_level> level;
   /// if not set, then parents enabled is used.
   std::optional<bool> enabled;
   // if empty, then parents appenders are used.
   std::vector<std::string> appenders;
};

struct logging_config {
   static logging_config default_config();
   std::vector<std::string> includes;
   std::vector<appender_config> appenders;
   std::vector<logger_config> loggers;
};

struct log_config {

   template <typename T> static bool register_appender(const std::string& type) {
      return register_appender(type, std::make_shared<detail::appender_factory_impl<T>>());
   }

   static bool register_appender(const std::string& type, const appender_factory::ptr& f);

   static logger get_logger(const std::string& name);
   static void update_logger(const std::string& name, logger& log);
   static void update_logger_with_default(const std::string& name, logger& log, const std::string& default_name);
   static void initialize_appenders();

   static bool configure_logging(const logging_config& l);

 private:
   static log_config& get();

   friend class logger;

   std::mutex log_mutex;
   std::unordered_map<std::string, appender_factory::ptr> appender_factory_map;
   std::unordered_map<std::string, appender::ptr> appender_map;
   std::unordered_map<std::string, logger> logger_map;
};

void configure_logging(const std::filesystem::path& log_config);
bool configure_logging(const logging_config& l);

} // namespace forge

export namespace forge {
BOOST_DESCRIBE_STRUCT(appender_config, (), (name, type, args))
BOOST_DESCRIBE_STRUCT(logger_config, (), (name, level, enabled, appenders))
BOOST_DESCRIBE_STRUCT(logging_config, (), (includes, appenders, loggers))
} // namespace forge
