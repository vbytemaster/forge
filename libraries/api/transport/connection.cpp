module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <memory>
#include <utility>

module forge.api.transport.connection;

namespace forge::api::transport {

connection::connection() = default;

connection::connection(forge::net::transport::stream stream, options value)
    : session_{std::make_shared<forge::api::stream::session>(
         std::move(stream), std::move(value))} {}

connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return session_ && session_->valid();
}

const options& connection::settings() const noexcept {
   static const auto defaults = options{};
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
connection::open_remote_invoker(
   forge::api::core::api_ref,
   forge::api::core::descriptor remote_descriptor) {
   return open_remote_invoker_impl(session_, std::move(remote_descriptor));
}

boost::asio::awaitable<std::shared_ptr<forge::api::core::remote_invoker>>
connection::open_remote_invoker_impl(
   std::shared_ptr<forge::api::stream::session> session,
   forge::api::core::descriptor remote_descriptor) {
   if (!session || !session->valid()) {
      FORGE_THROW_EXCEPTION(exceptions::cancelled,
                            "API transport connection is closed");
   }
   co_return std::make_shared<client>(std::move(session),
                                      std::move(remote_descriptor));
}

} // namespace forge::api::transport
