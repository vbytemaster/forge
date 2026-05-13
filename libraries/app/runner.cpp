module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

module fcl.app.runner;

import fcl.asio.blocking;
import fcl.config.document;
import fcl.app.application;
import fcl.app.application_shell;

namespace fcl::app {
namespace {

boost::asio::awaitable<void> wait_for_os_signal(application_shell& app, const run_options& options) {
   auto signals = boost::asio::signal_set{app.runtime().context()};
   if (options.handle_sigint) {
      signals.add(SIGINT);
   }
   if (options.handle_sigterm) {
      signals.add(SIGTERM);
   }

   auto error = boost::system::error_code{};
   co_await signals.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
   if (error) {
      throw std::runtime_error{"application signal wait failed: " + error.message()};
   }
}

void shutdown_with_timeout(application_shell& app, std::chrono::milliseconds timeout) {
   app.request_stop();
   if (app.state() == application_state::stopped) {
      return;
   }
   if (timeout.count() <= 0) {
      fcl::asio::blocking::run(app.runtime(), app.shutdown());
      return;
   }
   if (!fcl::asio::blocking::run_for(app.runtime(), app.shutdown(), timeout)) {
      throw std::runtime_error{"application shutdown timed out"};
   }
}

} // namespace

int run_application(application_shell& app, const fcl::config::document& document, run_options options) {
   auto exit_code = 0;
   auto failure = std::exception_ptr{};

   try {
      app.configure(document);
      fcl::asio::blocking::run(app.runtime(), app.startup());
      if (options.wait_for_stop) {
         fcl::asio::blocking::run(app.runtime(), options.wait_for_stop(app));
      } else if (options.handle_sigint || options.handle_sigterm) {
         fcl::asio::blocking::run(app.runtime(), wait_for_os_signal(app, options));
      } else {
         exit_code = app.run();
      }
   } catch (...) {
      failure = std::current_exception();
   }

   try {
      shutdown_with_timeout(app, options.shutdown_timeout);
   } catch (...) {
      if (!failure) {
         throw;
      }
   }

   if (failure) {
      std::rethrow_exception(failure);
   }
   return exit_code;
}

int run_application(std::unique_ptr<application_shell> app, const fcl::config::document& document,
                    run_options options) {
   if (!app) {
      throw std::invalid_argument{"application pointer must not be null"};
   }
   return run_application(*app, document, std::move(options));
}

} // namespace fcl::app
