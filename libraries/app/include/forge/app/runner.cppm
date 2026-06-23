module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <functional>
#include <memory>

export module forge.app.runner;

import forge.config.document;
import forge.app.application_shell;

export namespace forge::app {

using stop_waiter = std::function<boost::asio::awaitable<void>(application_shell&)>;

struct run_options {
   bool handle_sigint = true;
   bool handle_sigterm = true;
   std::chrono::milliseconds shutdown_timeout = std::chrono::seconds{10};
   stop_waiter wait_for_stop;
};

int run_application(application_shell& app, const forge::config::document& document, run_options options = {});
int run_application(std::unique_ptr<application_shell> app, const forge::config::document& document,
                    run_options options = {});

} // namespace forge::app
