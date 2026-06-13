module;
#include <memory>
#include <boost/describe.hpp>
#include <vector>

export module fcl.log.console_appender;

import fcl.log.appender;
import fcl.log.logger;
import fcl.log.log_message;
import fcl.variant.described;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

export namespace fcl {
class console_appender final : public appender {
 public:
   struct color {
      enum type {
         red,
         green,
         brown,
         blue,
         magenta,
         cyan,
         white,
         console_default,
      };
      BOOST_DESCRIBE_NESTED_ENUM(type, red, green, brown, blue, magenta, cyan, white, console_default)
   };

   struct stream {
      enum type { std_out, std_error };
      BOOST_DESCRIBE_NESTED_ENUM(type, std_out, std_error)
   };

   struct level_color {
      level_color(log_level l = log_level::all, color::type c = color::console_default) : level(l), color(c) {}

      log_level level;
      console_appender::color::type color;
      BOOST_DESCRIBE_CLASS(level_color, (), (level, color), (), ())
   };

   struct config {
      config() : stream(console_appender::stream::std_error), flush(true) {}

      console_appender::stream::type stream;
      std::vector<level_color> level_colors;
      bool flush;
      BOOST_DESCRIBE_CLASS(config, (), (stream, level_colors, flush), (), ())
   };

   explicit console_appender(const variant& args);
   explicit console_appender(const config& cfg);
   console_appender();

   ~console_appender();
   void initialize() override {}
   virtual void log(const log_message& m) override;

   void print(const std::string& text_to_print, color::type text_color = color::console_default);

   void configure(const config& cfg);

 private:
   class impl;
   std::unique_ptr<impl> my;
};
} // namespace fcl
