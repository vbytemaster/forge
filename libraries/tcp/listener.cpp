module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module fcl.tcp.listener;

namespace fcl::tcp {
namespace {

using asio_tcp = boost::asio::ip::tcp;

[[noreturn]] void throw_invalid_endpoint(const transport::endpoint& endpoint, std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, std::move(message),
                       fcl::exceptions::ctx("host", endpoint.host),
                       fcl::exceptions::ctx("port", endpoint.port),
                       fcl::exceptions::ctx("protocol", static_cast<int>(endpoint.protocol)));
}

[[noreturn]] void throw_invalid_options(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_listen_failed(const transport::endpoint& endpoint, const boost::system::error_code& error) {
   FCL_THROW_EXCEPTION(exceptions::listen_failed, "tcp listen failed",
                       fcl::exceptions::ctx("host", endpoint.host),
                       fcl::exceptions::ctx("port", endpoint.port),
                       fcl::exceptions::ctx("reason", error.message()));
}

void validate_options(const options& value) {
   if (value.read_chunk_size == 0) {
      throw_invalid_options("tcp read_chunk_size must be greater than zero");
   }
}

asio_tcp::endpoint to_bind_endpoint(const transport::endpoint& endpoint) {
   if (endpoint.protocol != transport::endpoint::protocol_kind::tcp) {
      throw_invalid_endpoint(endpoint, "tcp listener requires tcp endpoint protocol");
   }
   if (endpoint.host.empty()) {
      throw_invalid_endpoint(endpoint, "tcp listener requires non-empty host");
   }

   auto error = boost::system::error_code{};
   switch (endpoint.host_type) {
   case transport::endpoint::host_kind::ip4: {
      const auto address = boost::asio::ip::make_address_v4(endpoint.host, error);
      if (error) {
         throw_invalid_endpoint(endpoint, "tcp listener requires valid IPv4 host");
      }
      return asio_tcp::endpoint{address, endpoint.port};
   }
   case transport::endpoint::host_kind::ip6: {
      const auto address = boost::asio::ip::make_address_v6(endpoint.host, error);
      if (error) {
         throw_invalid_endpoint(endpoint, "tcp listener requires valid IPv6 host");
      }
      return asio_tcp::endpoint{address, endpoint.port};
   }
   case transport::endpoint::host_kind::dns:
   case transport::endpoint::host_kind::dns4:
   case transport::endpoint::host_kind::dns6:
      throw_invalid_endpoint(endpoint, "tcp listener cannot bind DNS host kind");
   }
   throw_invalid_endpoint(endpoint, "tcp listener received unsupported host kind");
}

[[nodiscard]] transport::endpoint from_asio_endpoint(const asio_tcp::endpoint& endpoint) {
   const auto address = endpoint.address();
   return transport::endpoint{.host_type = address.is_v6() ? transport::endpoint::host_kind::ip6
                                                           : transport::endpoint::host_kind::ip4,
                              .protocol = transport::endpoint::protocol_kind::tcp,
                              .host = address.to_string(),
                              .port = endpoint.port()};
}

void configure_socket(asio_tcp::socket& socket, const options& tcp_options) {
   auto error = boost::system::error_code{};
   socket.set_option(asio_tcp::no_delay{tcp_options.no_delay}, error);
   if (error) {
      FCL_THROW_EXCEPTION(exceptions::io_error, "failed to configure tcp no_delay",
                          fcl::exceptions::ctx("reason", error.message()));
   }
   socket.set_option(boost::asio::socket_base::keep_alive{tcp_options.keep_alive}, error);
   if (error) {
      FCL_THROW_EXCEPTION(exceptions::io_error, "failed to configure tcp keep_alive",
                          fcl::exceptions::ctx("reason", error.message()));
   }
}

} // namespace

struct listener::impl final : transport::detail::stream_listener_concept {
   impl(boost::asio::any_io_executor executor, transport::endpoint requested, transport::listen_options listen_options,
        options tcp_options_value)
       : acceptor(std::move(executor)), tcp_options(tcp_options_value) {
      validate_options(tcp_options);
      if (listen_options.limits.max_connections == 0) {
         throw_invalid_options("tcp listener max_connections must be greater than zero");
      }

      const auto bind_endpoint = to_bind_endpoint(requested);
      auto error = boost::system::error_code{};
      acceptor.open(bind_endpoint.protocol(), error);
      if (error) {
         throw_listen_failed(requested, error);
      }
      acceptor.set_option(boost::asio::socket_base::reuse_address{tcp_options.reuse_address}, error);
      if (error) {
         throw_listen_failed(requested, error);
      }
      acceptor.bind(bind_endpoint, error);
      if (error) {
         throw_listen_failed(requested, error);
      }
      const auto backlog = static_cast<int>(
          std::min<std::size_t>(listen_options.limits.max_connections,
                                static_cast<std::size_t>(boost::asio::socket_base::max_listen_connections)));
      acceptor.listen(backlog, error);
      if (error) {
         throw_listen_failed(requested, error);
      }
      local = from_asio_endpoint(acceptor.local_endpoint(error));
      if (error) {
         throw_listen_failed(requested, error);
      }
   }

   [[nodiscard]] bool valid() const noexcept override {
      return acceptor.is_open();
   }

   [[nodiscard]] transport::endpoint local_endpoint() const override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp listener");
      }
      return local;
   }

   boost::asio::awaitable<connection> async_accept_connection() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp listener");
      }
      auto socket = asio_tcp::socket{acceptor.get_executor()};
      auto error = boost::system::error_code{};
      co_await acceptor.async_accept(socket, boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         if (error == boost::asio::error::operation_aborted) {
            if (close_requested.load(std::memory_order_acquire)) {
               FCL_THROW_EXCEPTION(exceptions::closed, "tcp listener closed during accept");
            }
            FCL_THROW_EXCEPTION(exceptions::canceled, "tcp listener accept canceled");
         }
         FCL_THROW_EXCEPTION(exceptions::accept_failed, "tcp accept failed",
                             fcl::exceptions::ctx("reason", error.message()));
      }

      configure_socket(socket, tcp_options);
      co_return connection{std::move(socket), tcp_options};
   }

   boost::asio::awaitable<transport::stream_connection> async_accept() override {
      auto tcp_connection = co_await async_accept_connection();
      co_return std::move(tcp_connection).into_transport_stream();
   }

   boost::asio::awaitable<void> async_close() override {
      close_requested.store(true, std::memory_order_release);
      auto ignored = boost::system::error_code{};
      acceptor.close(ignored);
      co_return;
   }

   void cancel() override {
      auto ignored = boost::system::error_code{};
      acceptor.cancel(ignored);
   }

   asio_tcp::acceptor acceptor;
   transport::endpoint local;
   options tcp_options;
   std::atomic_bool close_requested = false;
};

listener::listener() = default;
listener::listener(boost::asio::any_io_executor executor, transport::endpoint local, transport::listen_options listen_options,
                   options tcp_options)
    : impl_(std::make_shared<impl>(std::move(executor), std::move(local), listen_options, tcp_options)) {}
listener::~listener() = default;
listener::listener(listener&&) noexcept = default;
listener& listener::operator=(listener&&) noexcept = default;

bool listener::valid() const noexcept {
   return impl_ && impl_->valid();
}

transport::endpoint listener::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp listener");
   }
   return impl_->local_endpoint();
}

boost::asio::awaitable<connection> listener::async_accept_connection() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp listener");
   }
   co_return co_await impl_->async_accept_connection();
}

boost::asio::awaitable<transport::stream_connection> listener::async_accept() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp listener");
   }
   co_return co_await impl_->async_accept();
}

boost::asio::awaitable<void> listener::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->async_close();
}

void listener::cancel() {
   if (valid()) {
      impl_->cancel();
   }
}

transport::stream_listener listener::as_transport() const {
   if (!valid()) {
      return {};
   }
   return transport::detail::stream_listener_access::make(impl_);
}

} // namespace fcl::tcp
