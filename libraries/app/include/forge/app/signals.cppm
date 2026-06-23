module;

#include <string>

#include <boost/signals2.hpp>

export module forge.app.signals;

export namespace forge::app {

struct application_signal {
   std::string name;
};

struct plugin_signal {
   std::string plugin;
};

class signal_bus {
 public:
   boost::signals2::signal<void(const application_signal&)> application_initializing;
   boost::signals2::signal<void(const application_signal&)> application_initialized;
   boost::signals2::signal<void(const application_signal&)> application_starting;
   boost::signals2::signal<void(const application_signal&)> application_started;
   boost::signals2::signal<void(const application_signal&)> application_stopping;
   boost::signals2::signal<void(const application_signal&)> application_stopped;

   boost::signals2::signal<void(const plugin_signal&)> plugin_initializing;
   boost::signals2::signal<void(const plugin_signal&)> plugin_initialized;
   boost::signals2::signal<void(const plugin_signal&)> plugin_starting;
   boost::signals2::signal<void(const plugin_signal&)> plugin_started;
   boost::signals2::signal<void(const plugin_signal&)> plugin_stopping;
   boost::signals2::signal<void(const plugin_signal&)> plugin_stopped;
};

} // namespace forge::app
