module;

#include <fcl/exception/macros.hpp>

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.stcp.connector;

namespace fcl::stcp {

struct connector::impl final {
   impl(boost::asio::any_io_executor executor_value, client_options tls_options_value, tcp::options tcp_options_value)
       : tcp_connector(std::move(executor_value), tcp_options_value), tls_options(std::move(tls_options_value)) {}

   [[nodiscard]] bool valid() const noexcept {
      return tcp_connector.valid();
   }

   boost::asio::awaitable<connection> async_connect_connection(transport::endpoint remote,
                                                               transport::connect_options connect_options) {
      auto source = co_await tcp_connector.async_connect_connection(std::move(remote), connect_options);
      co_return co_await async_upgrade_client(std::move(source), tls_options);
   }

   tcp::connector tcp_connector;
   client_options tls_options;
};

connector::connector() = default;
connector::connector(boost::asio::any_io_executor executor, client_options tls_options, tcp::options tcp_options)
    : impl_(std::make_shared<impl>(std::move(executor), std::move(tls_options), tcp_options)) {}
connector::~connector() = default;
connector::connector(connector&&) noexcept = default;
connector& connector::operator=(connector&&) noexcept = default;

bool connector::valid() const noexcept {
   return impl_ && impl_->valid();
}

boost::asio::awaitable<connection> connector::async_connect_connection(transport::endpoint remote,
                                                                       transport::connect_options options) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid stcp connector");
   }
   co_return co_await impl_->async_connect_connection(std::move(remote), options);
}

boost::asio::awaitable<transport::stream_connection> connector::async_connect(transport::endpoint remote,
                                                                              transport::connect_options options) {
   auto tls_connection = co_await async_connect_connection(std::move(remote), options);
   co_return std::move(tls_connection).into_transport_stream();
}

void connector::cancel() {
   if (valid()) {
      impl_->tcp_connector.cancel();
   }
}

} // namespace fcl::stcp
