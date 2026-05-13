module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <exception>
#include <memory>
#include <mutex>
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

bool run_shutdown_to_completion(application_shell& app, std::chrono::milliseconds timeout) {
   struct shutdown_state {
      std::mutex mutex;
      std::condition_variable ready;
      bool done = false;
      bool timed_out = false;
      std::exception_ptr error;
   };

   auto state = std::make_shared<shutdown_state>();
   auto timer = std::make_shared<boost::asio::steady_timer>(app.runtime().context());
   timer->expires_after(timeout);

   boost::asio::co_spawn(
      app.runtime().context(),
      [state, timer]() -> boost::asio::awaitable<void> {
         auto error = boost::system::error_code{};
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!error) {
            const auto lock = std::scoped_lock{state->mutex};
            if (!state->done) {
               state->timed_out = true;
            }
         }
      },
      boost::asio::detached);

   boost::asio::co_spawn(
      app.runtime().context(),
      [&app, state, timer]() -> boost::asio::awaitable<void> {
         auto error = std::exception_ptr{};
         try {
            co_await app.shutdown();
         } catch (...) {
            error = std::current_exception();
         }

         {
            const auto lock = std::scoped_lock{state->mutex};
            state->error = std::move(error);
            state->done = true;
         }
         timer->cancel();
         state->ready.notify_all();
      },
      boost::asio::detached);

   auto lock = std::unique_lock{state->mutex};
   state->ready.wait(lock, [&] {
      return state->done;
   });

   if (state->timed_out) {
      return false;
   }
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   return true;
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
   if (!run_shutdown_to_completion(app, timeout)) {
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
