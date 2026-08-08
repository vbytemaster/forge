module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.api.transport.server;

import forge.api.stream.server;
import forge.net.transport.exceptions;

namespace forge::api::transport {
namespace {

[[nodiscard]] bool is_clean_close(const forge::exceptions::base& error) noexcept {
   return forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::closed) ||
          forge::net::transport::exceptions::is(error, forge::net::transport::exceptions::code::canceled);
}

} // namespace

boost::asio::awaitable<void> serve_session(forge::net::transport::session session, forge::api::core::binding_plan plan,
                                           session_options value) {
   if (value.max_concurrent_streams == 0) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "API transport max concurrent streams must be positive");
   }

   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = boost::asio::make_strand(executor);
   auto wake = std::make_shared<boost::asio::steady_timer>(strand);
   auto slots = std::make_shared<std::size_t>(0);
   wake->expires_at(boost::asio::steady_timer::time_point::max());
   auto reserve_slot = [strand, wake, slots,
                        max = value.max_concurrent_streams]()
      -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(strand, boost::asio::use_awaitable);
      while (*slots >= max) {
         wake->expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await wake->async_wait(boost::asio::redirect_error(
            boost::asio::use_awaitable, error));
      }
      ++*slots;
   };
   auto release_slot = [strand, wake, slots]()
      -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(strand, boost::asio::use_awaitable);
      if (*slots > 0) {
         --*slots;
      }
      wake->cancel();
   };
   auto wait_for_drain = [strand, wake, slots]()
      -> boost::asio::awaitable<void> {
      co_await boost::asio::dispatch(strand, boost::asio::use_awaitable);
      while (*slots > 0) {
         wake->expires_at(boost::asio::steady_timer::time_point::max());
         auto error = boost::system::error_code{};
         co_await wake->async_wait(boost::asio::redirect_error(
            boost::asio::use_awaitable, error));
      }
   };

   auto accepting = true;
   while (accepting) {
      auto reserved = false;
      auto release_reserved = false;
      auto pending_error = std::exception_ptr{};
      try {
         co_await reserve_slot();
         reserved = true;
         auto stream = co_await session.async_accept_stream();
         boost::asio::co_spawn(
             executor,
             [release_slot, stream = std::move(stream), plan, stream_options = value.stream]() mutable
             -> boost::asio::awaitable<void> {
                try {
                   co_await forge::api::stream::serve_stream(std::move(stream), std::move(plan), stream_options);
                } catch (const forge::exceptions::base&) {
                   // A bad API stream closes that stream; the session accept loop owns admission.
                } catch (...) {
                   // Detached stream failures must still release their reserved admission slot.
                }
                co_await release_slot();
             },
             boost::asio::detached);
      } catch (const forge::exceptions::base& error) {
         if (reserved) {
            release_reserved = true;
         }
         if (is_clean_close(error)) {
            accepting = false;
         } else {
            pending_error = std::current_exception();
         }
      } catch (...) {
         if (reserved) {
            release_reserved = true;
         }
         pending_error = std::current_exception();
      }
      if (release_reserved) {
         co_await release_slot();
      }
      if (pending_error) {
         std::rethrow_exception(pending_error);
      }
   }
   co_await wait_for_drain();
}

} // namespace forge::api::transport
