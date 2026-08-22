module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

module forge.net.p2p.node;

import forge.exceptions;
import forge.asio.notification;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/cancellation_latch.hxx"
#include "details/dht_fanout.hxx"
#include "details/dht_fanout_state.hxx"
#include "details/operation_deadline.hxx"

namespace forge::net::p2p::detail::dht_fanout {
namespace {

void reach_test_hook(const test_hooks& hooks, test_stage stage) {
   if (hooks.reach != nullptr) {
      hooks.reach(hooks.context, stage);
   }
}

} // namespace

boost::asio::awaitable<result> run(boost::asio::io_context& context, request value, operation invoke) {
   namespace asio = boost::asio;

   if (value.concurrency == 0 || value.success_target == 0 || value.timeout.count() <= 0 || value.operation.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "DHT fanout concurrency, success target, timeout and operation must be positive");
   }

   auto unique = std::set<peer_id>{};
   value.peers.erase(std::remove_if(value.peers.begin(), value.peers.end(),
                                    [&](const auto& peer) { return !unique.insert(peer).second; }),
                     value.peers.end());

   const auto started = std::chrono::steady_clock::now();
   const auto operation_name = value.operation;
   auto executor = asio::any_io_executor{co_await asio::this_coro::executor};
   auto strand = asio::make_strand(executor);
   auto state = std::make_shared<::forge::net::p2p::detail::dht_fanout_state>(value.concurrency);
   auto callable = std::make_shared<operation>(std::move(invoke));
   auto deadline = operation_deadline{context, value.timeout};

   deadline.arm([state, hooks = value.hooks] {
      state->request_stop();
      reach_test_hook(hooks, test_stage::after_stop_request);
   });
   reach_test_hook(value.hooks, test_stage::before_coordinator_spawn);

   auto output = result{};
   auto terminal_error = std::exception_ptr{};
   try {
      output = co_await asio::co_spawn(
          strand,
          [value = std::move(value), started, strand, state, callable]() mutable -> asio::awaitable<result> {
             auto out = result{};
             auto next = std::size_t{};

             const auto launch = [&](peer_id peer) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
                if (elapsed >= value.timeout) {
                   throw_operation_timeout(value.operation);
                }
                const auto remaining = value.timeout - elapsed;
                auto cancellation = std::make_shared<cancellation_latch>();
                state->publish(peer, cancellation);
                ++out.attempted;
                try {
                   asio::co_spawn(
                       strand,
                       [callable, peer, remaining, cancellation = std::move(cancellation)]()
                           -> asio::awaitable<bool> {
                          co_return co_await (*callable)(peer, remaining, std::move(cancellation));
                       },
                       asio::bind_executor(strand, [state, peer](std::exception_ptr error, bool succeeded) noexcept {
                          state->complete(peer, succeeded, std::move(error));
                       }));
                } catch (...) {
                   state->abandon(peer);
                   throw;
                }
             };

             auto failure = std::exception_ptr{};
             try {
                while (out.succeeded < value.success_target && !state->stop_requested()) {
                   while (state->active_count() < value.concurrency && next < value.peers.size() &&
                          !state->stop_requested()) {
                      launch(value.peers[next++]);
                   }
                   if (state->active_count() == 0 || state->stop_requested()) {
                      break;
                   }

                   auto completion = co_await state->async_next_or_stop();
                   if (!completion) {
                      break;
                   }
                   if (completion->error) {
                      std::rethrow_exception(completion->error);
                   }
                   out.succeeded += static_cast<std::size_t>(completion->succeeded);
                }
             } catch (...) {
                failure = std::current_exception();
             }

             if (state->active_count() != 0) {
                co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
                state->cancel_active();
                while (state->active_count() != 0) {
                   static_cast<void>(co_await state->async_next_completion());
                }
             }
             if (failure) {
                std::rethrow_exception(failure);
             }
             co_return out;
          },
          asio::use_awaitable);
   } catch (...) {
      terminal_error = std::current_exception();
   }

   const auto completed = deadline.finish();
   if (!completed) {
      throw_operation_timeout(operation_name);
   }
   if (terminal_error) {
      std::rethrow_exception(terminal_error);
   }
   co_return output;
}

} // namespace forge::net::p2p::detail::dht_fanout
