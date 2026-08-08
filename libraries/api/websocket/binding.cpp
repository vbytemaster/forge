module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <limits>
#include <utility>

module forge.api.websocket.binding;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.stream.server;
import forge.api.websocket.stream;
import forge.net.websocket.exceptions;

namespace forge::api::websocket {
namespace {

[[nodiscard]] forge::api::core::capability
capability_for(forge::api::core::method_kind kind) {
   switch (kind) {
      case forge::api::core::method_kind::unary:
         return forge::api::core::capability::unary;
      case forge::api::core::method_kind::server_stream:
         return forge::api::core::capability::server_stream;
      case forge::api::core::method_kind::client_stream:
         return forge::api::core::capability::client_stream;
      case forge::api::core::method_kind::bidirectional_stream:
         return forge::api::core::capability::bidirectional_stream;
   }
   FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                         "WebSocket API descriptor has an unsupported method kind");
}

void validate_descriptor(const forge::api::core::descriptor& descriptor,
                         forge::api::core::capability_set capabilities) {
   for (const auto& method : descriptor.methods) {
      if (!capabilities.supports(capability_for(method.kind))) {
         FORGE_THROW_EXCEPTION(
            forge::api::core::exceptions::incompatible_version,
            "WebSocket API binding does not support descriptor method kind",
            forge::exceptions::ctx("api", descriptor.id.value),
            forge::exceptions::ctx("method", method.name));
      }
   }
}

void validate_plan(const forge::api::core::binding_plan& plan,
                   forge::api::core::capability_set capabilities) {
   if (plan.local == nullptr) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::incompatible_version,
                            "WebSocket API binding has no local registry");
   }
   for (const auto& descriptor : plan.exports) {
      validate_descriptor(descriptor, capabilities);
   }
}

[[nodiscard]] std::uint32_t checked_frame_size(std::size_t value) {
   if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::protocol_error,
                            "WebSocket API frame size is out of range");
   }
   return static_cast<std::uint32_t>(value);
}

boost::asio::awaitable<void> accept_connection(
   forge::net::websocket::connection::ptr socket,
   forge::net::transport::stream stream,
   forge::api::core::binding_plan plan,
   forge::api::stream::options options) {
   co_await socket->ping();
   co_await forge::api::stream::serve_stream(
      std::move(stream), std::move(plan), std::move(options));
}

} // namespace

forge::api::core::capability_set binding_capabilities() noexcept {
   return forge::api::core::capability_set{
      .bits = static_cast<std::uint64_t>(forge::api::core::capability::unary) |
              static_cast<std::uint64_t>(forge::api::core::capability::server_stream) |
              static_cast<std::uint64_t>(forge::api::core::capability::client_stream) |
              static_cast<std::uint64_t>(forge::api::core::capability::bidirectional_stream) |
              static_cast<std::uint64_t>(forge::api::core::capability::stream_window),
   };
}

api_binding::api_binding(forge::api::core::binding_plan plan,
                         forge::api::stream::options options)
    : plan_{std::move(plan)}, options_{std::move(options)} {
   options_.capabilities = binding_capabilities();
   validate_plan(plan_, options_.capabilities);
}

boost::asio::awaitable<void>
api_binding::accept(forge::net::websocket::connection::ptr connection) const {
   auto socket = connection;
   auto stream = as_transport_stream(std::move(connection),
                                     options_.max_frame_size,
                                     options_.max_buffered_bytes);
   return accept_connection(std::move(socket), std::move(stream), plan_,
                            options_);
}

boost::asio::awaitable<forge::api::websocket::connection>
api_binding::connect(forge::net::websocket::connection::ptr connection) const {
   co_return forge::api::websocket::connection{std::move(connection), options_};
}

const forge::api::core::codec_id& api_binding::codec() const noexcept {
   return options_.codec;
}

std::size_t api_binding::max_frame_size() const noexcept {
   return options_.max_frame_size;
}

api_backpressure_options api_binding::backpressure() const noexcept {
   return {
      .max_inflight = options_.max_inflight,
      .max_buffered_bytes = options_.max_buffered_bytes,
   };
}

const forge::api::stream::options& api_binding::options() const noexcept {
   return options_;
}

api_builder& api_builder::use(forge::api::core::binding_plan plan) {
   plan_ = std::move(plan);
   return *this;
}

api_builder& api_builder::codec(forge::api::core::codec_id value) {
   options_.codec = std::move(value);
   return *this;
}

api_builder& api_builder::max_frame_size(std::size_t value) {
   options_.max_frame_size = checked_frame_size(value);
   return *this;
}

api_builder& api_builder::backpressure(api_backpressure_options value) {
   options_.max_inflight = value.max_inflight;
   options_.max_buffered_bytes = value.max_buffered_bytes;
   return *this;
}

api_builder& api_builder::deadline(std::chrono::milliseconds value) {
   options_.deadline = value;
   return *this;
}

api_builder& api_builder::initial_window(std::uint32_t items,
                                         std::uint64_t bytes) {
   options_.initial_window_items = items;
   options_.initial_window_bytes = bytes;
   return *this;
}

api_binding api_builder::build() {
   options_.capabilities = binding_capabilities();
   return api_binding{std::move(plan_), std::move(options_)};
}

api_builder api() {
   return {};
}

} // namespace forge::api::websocket
