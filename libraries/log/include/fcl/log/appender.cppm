module;
#include <memory>

export module fcl.log.appender;

import fcl.log.log_message;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

export namespace fcl {
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
} // namespace fcl
