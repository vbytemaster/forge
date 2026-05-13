module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <functional>
#include <memory>

export module fcl.app.runner;

import fcl.config.document;
import fcl.app.application_shell;

export namespace fcl::app {

using stop_waiter = std::function<boost::asio::awaitable<void>(application_shell&)>;

struct run_options {
   bool handle_sigint = true;
   bool handle_sigterm = true;
   std::chrono::milliseconds shutdown_timeout = std::chrono::seconds{10};
   stop_waiter wait_for_stop;
};

int run_application(application_shell& app, const fcl::config::document& document, run_options options = {});
int run_application(std::unique_ptr<application_shell> app, const fcl::config::document& document,
                    run_options options = {});

} // namespace fcl::app
