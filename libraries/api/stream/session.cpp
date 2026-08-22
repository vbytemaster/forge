module;

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module forge.api.stream.session;

import forge.asio.notification;

#include "details/session_impl.hxx"

namespace forge::api::stream {

session::session() = default;

session::session(forge::net::transport::stream stream, options value)
    : impl_{std::make_shared<impl>(std::move(stream), std::move(value))} {}

session::session(forge::net::transport::stream stream, forge::api::core::binding_plan plan, options value,
                 forge::api::core::metadata trusted_metadata)
    : impl_{std::make_shared<impl>(std::move(stream), std::move(plan), std::move(value), std::move(trusted_metadata))} {
}

session::~session() {
   cancel();
}
session::session(session&&) noexcept = default;
session& session::operator=(session&& other) noexcept {
   if (this != &other) {
      cancel();
      impl_ = std::move(other.impl_);
   }
   return *this;
}

bool session::valid() const noexcept {
   return impl_ && impl_->valid();
}

const options& session::settings() const noexcept {
   static const auto defaults = options{};
   return impl_ ? impl_->settings : defaults;
}

boost::asio::awaitable<forge::api::core::frame>
session::async_call(forge::api::core::frame request, call_options value,
                    std::optional<forge::api::core::method_descriptor> descriptor) {
   return async_call_impl(impl_, std::move(request), forge::api::core::method_kind::unary, {}, {}, std::move(value),
                          descriptor);
}

boost::asio::awaitable<forge::api::core::frame>
session::async_stream_call(forge::api::core::frame request, forge::api::core::method_kind kind,
                           std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
                           std::shared_ptr<forge::api::core::detail::stream_endpoint> output, call_options value,
                           std::optional<forge::api::core::method_descriptor> descriptor) {
   return async_call_impl(impl_, std::move(request), kind, std::move(input), std::move(output), std::move(value),
                          descriptor);
}

boost::asio::awaitable<forge::api::core::frame>
session::async_call_impl(std::shared_ptr<impl> self, forge::api::core::frame request,
                         forge::api::core::method_kind kind,
                         std::shared_ptr<forge::api::core::detail::stream_endpoint> input,
                         std::shared_ptr<forge::api::core::detail::stream_endpoint> output, call_options value,
                         std::optional<forge::api::core::method_descriptor> descriptor) {
   if (!self) {
      throw forge::api::core::exceptions::cancelled{"API stream session is closed"};
   }
   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = self->ensure_strand(executor);
   co_return co_await boost::asio::co_spawn(
       strand,
       [self, request = std::move(request), kind, input = std::move(input), output = std::move(output),
        value = std::move(value),
        descriptor = std::move(descriptor)]() mutable -> boost::asio::awaitable<forge::api::core::frame> {
          const auto executor = co_await boost::asio::this_coro::executor;
          self->initialize_on_strand(self->ensure_strand(executor));
          co_return co_await self->async_call_on_strand(std::move(request), kind, std::move(input), std::move(output),
                                                        std::move(value), descriptor ? &*descriptor : nullptr);
       },
       boost::asio::use_awaitable);
}

boost::asio::awaitable<void> session::async_serve() {
   return async_serve_impl(impl_);
}

boost::asio::awaitable<void> session::async_serve_impl(std::shared_ptr<impl> self) {
   if (!self) {
      throw forge::api::core::exceptions::cancelled{"API stream session is closed"};
   }
   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = self->ensure_strand(executor);
   co_await boost::asio::co_spawn(
       strand,
       [self, strand]() -> boost::asio::awaitable<void> {
          self->initialize_on_strand(strand);
          co_await self->async_serve_on_strand();
       },
       boost::asio::use_awaitable);
}

boost::asio::awaitable<void> session::async_close() {
   return async_close_impl(impl_);
}

boost::asio::awaitable<void> session::async_close_impl(std::shared_ptr<impl> self) {
   if (!self) {
      co_return;
   }
   const auto executor = co_await boost::asio::this_coro::executor;
   auto strand = self->ensure_strand(executor);
   co_await boost::asio::co_spawn(
       strand,
       [self, strand]() -> boost::asio::awaitable<void> {
          self->initialize_on_strand(strand);
          co_await self->async_close_on_strand();
       },
       boost::asio::use_awaitable);
}

void session::cancel() noexcept {
   auto self = impl_;
   if (!self) {
      return;
   }
   self->closed.store(true, std::memory_order_release);
   const auto executor = self->current_strand();
   if (!executor) {
      try {
         self->stream.cancel();
      } catch (...) {
         // The transport is already terminal from the public session view.
      }
      return;
   }
   try {
      boost::asio::dispatch(*executor, [self] {
         self->fail_session(
             std::make_exception_ptr(forge::api::core::exceptions::cancelled{"API stream session was cancelled"}));
         self->stop_transport();
      });
   } catch (...) {
      try {
         self->stream.cancel();
      } catch (...) {
         // Cancellation remains best effort from a noexcept path.
      }
   }
}

} // namespace forge::api::stream
