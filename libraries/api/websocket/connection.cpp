module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <memory>
#include <utility>

module forge.api.websocket.connection;

import forge.api.core.exceptions;
import forge.api.stream.session;
import forge.api.transport.client;
import forge.api.websocket.stream;

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
            "WebSocket API connection does not support descriptor method kind",
            forge::exceptions::ctx("api", descriptor.id.value),
            forge::exceptions::ctx("method", method.name));
      }
   }
}

} // namespace

connection::connection() = default;

connection::connection(forge::net::websocket::connection::ptr connection,
                       forge::api::stream::options options)
    : capabilities_{options.capabilities},
      connection_{connection},
      session_{std::make_shared<forge::api::stream::session>(
         as_transport_stream(std::move(connection), options.max_frame_size,
                             options.max_buffered_bytes),
         std::move(options))} {}

connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return session_ && session_->valid();
}

const forge::api::stream::options& connection::settings() const noexcept {
   static const auto defaults = forge::api::stream::options{};
   return session_ ? session_->settings() : defaults;
}

boost::asio::awaitable<void> connection::async_close() {
   return async_close_impl(session_);
}

boost::asio::awaitable<void>
connection::async_close_impl(
   std::shared_ptr<forge::api::stream::session> session) {
   if (session) {
      co_await session->async_close();
   }
}

void connection::cancel() noexcept {
   if (session_) {
      session_->cancel();
   }
}

boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
connection::open_remote_invoker(forge::api::core::api_ref,
                                forge::api::core::descriptor remote_descriptor) {
   return open_remote_invoker_impl(session_, connection_,
                                   std::move(remote_descriptor), capabilities_);
}

boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
connection::open_remote_invoker_impl(
   std::shared_ptr<forge::api::stream::session> session,
   forge::net::websocket::connection::ptr connection,
   forge::api::core::descriptor remote_descriptor,
   forge::api::core::capability_set capabilities) {
   if (!session || !session->valid() || !connection) {
      FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled,
                            "WebSocket API connection is closed");
   }
   validate_descriptor(remote_descriptor, capabilities);
   co_await connection->ping();
   co_return std::make_shared<forge::api::transport::client>(
      std::move(session), std::move(remote_descriptor));
}

} // namespace forge::api::websocket
