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
#include <thread>
#include <utility>

module fcl.app.runner;

import fcl.asio.blocking;
import fcl.app.exceptions;
import fcl.app.application;
import fcl.app.application_shell;
import fcl.config.document;

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
      throw exceptions::startup_failed{"application signal wait failed: " + error.message()};
   }
}

struct shutdown_state {
   std::mutex mutex;
   std::condition_variable ready;
   bool done = false;
   bool timed_out = false;
   std::exception_ptr error;
};

struct shutdown_owner {
   explicit shutdown_owner(std::unique_ptr<application_shell> input) : app{std::move(input)} {}

   std::unique_ptr<application_shell> app;
};

void keep_owner_until_shutdown_finishes(std::shared_ptr<shutdown_state> state, std::shared_ptr<shutdown_owner> owner) {
   if (!owner) {
      return;
   }
   std::thread{[state = std::move(state), owner = std::move(owner)]() mutable {
      auto lock = std::unique_lock{state->mutex};
      state->ready.wait(lock, [&] {
         return state->done;
      });
      lock.unlock();
      owner.reset();
   }}.detach();
}

bool run_shutdown_until_timeout(application_shell& app, std::chrono::milliseconds timeout,
                                bool allow_async_shutdown_tail,
                                std::shared_ptr<shutdown_owner> owner = {}) {
   auto state = std::make_shared<shutdown_state>();
   auto timer = std::make_shared<boost::asio::steady_timer>(app.runtime().context());
   timer->expires_after(timeout);

   boost::asio::co_spawn(
      app.runtime().context(),
      [state, timer]() -> boost::asio::awaitable<void> {
         auto error = boost::system::error_code{};
         co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!error) {
            {
               const auto lock = std::scoped_lock{state->mutex};
               if (!state->done) {
                  state->timed_out = true;
               }
            }
            state->ready.notify_all();
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
      return state->done || state->timed_out;
   });

   if (state->timed_out) {
      if (!state->done) {
         if (allow_async_shutdown_tail) {
            keep_owner_until_shutdown_finishes(state, std::move(owner));
            return false;
         }
         state->ready.wait(lock, [&] {
            return state->done;
         });
      }
      auto error = state->error;
      lock.unlock();
      if (error) {
         std::rethrow_exception(error);
      }
      return false;
   }
   auto error = state->error;
   lock.unlock();
   if (error) {
      std::rethrow_exception(error);
   }
   return true;
}

void shutdown_with_timeout(application_shell& app, std::chrono::milliseconds timeout,
                           bool allow_async_shutdown_tail, std::shared_ptr<shutdown_owner> owner = {}) {
   app.request_stop();
   if (app.state() == application_state::stopped) {
      return;
   }
   if (timeout.count() <= 0) {
      fcl::asio::blocking::run(app.runtime(), app.shutdown());
      return;
   }
   if (!run_shutdown_until_timeout(app, timeout, allow_async_shutdown_tail, std::move(owner))) {
      throw exceptions::shutdown_failed{"application shutdown timed out"};
   }
}

int run_application_impl(application_shell& app, const fcl::config::document& document, run_options options,
                         std::shared_ptr<shutdown_owner> owner) {
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
      const auto allow_async_shutdown_tail = static_cast<bool>(owner);
      shutdown_with_timeout(app, options.shutdown_timeout, allow_async_shutdown_tail, std::move(owner));
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

} // namespace

int run_application(application_shell& app, const fcl::config::document& document, run_options options) {
   return run_application_impl(app, document, std::move(options), {});
}

int run_application(std::unique_ptr<application_shell> app, const fcl::config::document& document,
                    run_options options) {
   if (!app) {
      throw std::invalid_argument{"application pointer must not be null"};
   }
   auto owner = std::make_shared<shutdown_owner>(std::move(app));
   return run_application_impl(*owner->app, document, std::move(options), owner);
}

} // namespace fcl::app
