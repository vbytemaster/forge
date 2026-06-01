module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.stcp.listener;

namespace fcl::stcp {

struct listener::impl final {
   impl(boost::asio::any_io_executor executor, transport::endpoint local, server_options tls_options_value,
        transport::listen_options listen_options, tcp::options tcp_options)
       : tcp_listener(std::move(executor), std::move(local), listen_options, tcp_options),
         tls_options(std::move(tls_options_value)) {}

   [[nodiscard]] bool valid() const noexcept {
      return tcp_listener.valid();
   }

   [[nodiscard]] transport::endpoint local_endpoint() const {
      return tcp_listener.local_endpoint();
   }

   boost::asio::awaitable<connection> async_accept_connection() {
      auto source = co_await tcp_listener.async_accept_connection();
      co_return co_await async_upgrade_server(std::move(source), tls_options);
   }

   tcp::listener tcp_listener;
   server_options tls_options;
};

listener::listener() = default;
listener::listener(boost::asio::any_io_executor executor, transport::endpoint local, server_options tls_options,
                   transport::listen_options listen_options, tcp::options tcp_options)
    : impl_(std::make_shared<impl>(std::move(executor), std::move(local), std::move(tls_options), listen_options,
                                   tcp_options)) {}
listener::~listener() = default;
listener::listener(listener&&) noexcept = default;
listener& listener::operator=(listener&&) noexcept = default;

bool listener::valid() const noexcept {
   return impl_ && impl_->valid();
}

transport::endpoint listener::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp listener");
   }
   return impl_->local_endpoint();
}

boost::asio::awaitable<connection> listener::async_accept_connection() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp listener");
   }
   co_return co_await impl_->async_accept_connection();
}

boost::asio::awaitable<transport::stream_connection> listener::async_accept() {
   auto tls_connection = co_await async_accept_connection();
   co_return std::move(tls_connection).into_transport_stream();
}

boost::asio::awaitable<void> listener::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->tcp_listener.async_close();
}

void listener::cancel() {
   if (valid()) {
      impl_->tcp_listener.cancel();
   }
}

} // namespace fcl::stcp
