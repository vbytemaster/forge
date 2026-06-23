module;
#include <memory>

export module forge.log.appender;

import forge.log.log_message;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

export namespace forge {
class appender;

class appender_factory {
 public:
   typedef std::shared_ptr<appender_factory> ptr;

   virtual ~appender_factory() = default;
   virtual std::shared_ptr<appender> create(const variant& args) = 0;
};

namespace detail {
template <typename T> class appender_factory_impl : public appender_factory {
 public:
   std::shared_ptr<appender> create(const variant& args) override {
      return std::shared_ptr<appender>(new T(args));
   }
};
} // namespace detail

class appender {
 public:
   typedef std::shared_ptr<appender> ptr;

   virtual ~appender() = default;
   virtual void initialize() = 0;
   virtual void log(const log_message& m) = 0;
};
} // namespace forge
