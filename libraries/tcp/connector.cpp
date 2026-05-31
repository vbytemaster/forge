module;

#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module fcl.tcp.connector;

namespace fcl::tcp {
namespace {

using asio_tcp = boost::asio::ip::tcp;

[[noreturn]] void throw_invalid_endpoint(const transport::endpoint& endpoint, std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_endpoint, std::move(message),
                       fcl::exception::ctx("host", endpoint.host),
                       fcl::exception::ctx("port", endpoint.port),
                       fcl::exception::ctx("protocol", static_cast<int>(endpoint.protocol)));
}

[[noreturn]] void throw_invalid_options(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_connect_failed(const transport::endpoint& endpoint, const boost::system::error_code& error) {
   FCL_THROW_EXCEPTION(exceptions::connect_failed, "tcp connect failed",
                       fcl::exception::ctx("host", endpoint.host),
                       fcl::exception::ctx("port", endpoint.port),
                       fcl::exception::ctx("reason", error.message()));
}

void validate_options(const options& value) {
   if (value.read_chunk_size == 0) {
      throw_invalid_options("tcp read_chunk_size must be greater than zero");
   }
}

void validate_remote_endpoint(const transport::endpoint& endpoint) {
   if (endpoint.protocol != transport::endpoint::protocol_kind::tcp) {
      throw_invalid_endpoint(endpoint, "tcp connector requires tcp endpoint protocol");
   }
   if (endpoint.host.empty()) {
      throw_invalid_endpoint(endpoint, "tcp connector requires non-empty host");
   }
   if (endpoint.port == 0) {
      throw_invalid_endpoint(endpoint, "tcp connector requires non-zero remote port");
   }

   auto error = boost::system::error_code{};
   switch (endpoint.host_type) {
   case transport::endpoint::host_kind::ip4:
      (void)boost::asio::ip::make_address_v4(endpoint.host, error);
      if (error) {
         throw_invalid_endpoint(endpoint, "tcp connector requires valid IPv4 host");
      }
      return;
   case transport::endpoint::host_kind::ip6:
      (void)boost::asio::ip::make_address_v6(endpoint.host, error);
      if (error) {
         throw_invalid_endpoint(endpoint, "tcp connector requires valid IPv6 host");
      }
      return;
   case transport::endpoint::host_kind::dns:
   case transport::endpoint::host_kind::dns4:
   case transport::endpoint::host_kind::dns6:
      return;
   }
   throw_invalid_endpoint(endpoint, "tcp connector received unsupported host kind");
}

void configure_socket(asio_tcp::socket& socket, const options& tcp_options) {
   auto error = boost::system::error_code{};
   socket.set_option(asio_tcp::no_delay{tcp_options.no_delay}, error);
   if (error) {
      FCL_THROW_EXCEPTION(exceptions::io_error, "failed to configure tcp no_delay",
                          fcl::exception::ctx("reason", error.message()));
   }
   socket.set_option(boost::asio::socket_base::keep_alive{tcp_options.keep_alive}, error);
   if (error) {
      FCL_THROW_EXCEPTION(exceptions::io_error, "failed to configure tcp keep_alive",
                          fcl::exception::ctx("reason", error.message()));
   }
}

[[nodiscard]] std::vector<asio_tcp::endpoint> filter_results(asio_tcp::resolver::results_type results,
                                                             transport::endpoint::host_kind host_type) {
   auto out = std::vector<asio_tcp::endpoint>{};
   for (const auto& entry : results) {
      const auto endpoint = entry.endpoint();
      if (host_type == transport::endpoint::host_kind::dns4 && !endpoint.address().is_v4()) {
         continue;
      }
      if (host_type == transport::endpoint::host_kind::dns6 && !endpoint.address().is_v6()) {
         continue;
      }
      out.push_back(endpoint);
   }
   return out;
}

} // namespace

struct connector::impl final : transport::detail::stream_connector_concept {
   impl(boost::asio::any_io_executor executor_value, options tcp_options_value)
       : executor(std::move(executor_value)), tcp_options(tcp_options_value) {
      validate_options(tcp_options);
   }

   [[nodiscard]] bool valid() const noexcept override {
      return active.load(std::memory_order_acquire);
   }

   boost::asio::awaitable<connection> async_connect_connection(transport::endpoint remote) {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connector");
      }
      validate_remote_endpoint(remote);

      auto socket = std::make_shared<asio_tcp::socket>(executor);
      auto resolver = std::make_shared<asio_tcp::resolver>(executor);
      {
         const auto lock = std::scoped_lock{mutex};
         sockets.push_back(socket);
         resolvers.push_back(resolver);
      }

      auto error = boost::system::error_code{};
      if (remote.host_type == transport::endpoint::host_kind::ip4) {
         const auto address = boost::asio::ip::make_address_v4(remote.host, error);
         if (error) {
            throw_invalid_endpoint(remote, "tcp connector requires valid IPv4 host");
         }
         co_await socket->async_connect(asio_tcp::endpoint{address, remote.port},
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      } else if (remote.host_type == transport::endpoint::host_kind::ip6) {
         const auto address = boost::asio::ip::make_address_v6(remote.host, error);
         if (error) {
            throw_invalid_endpoint(remote, "tcp connector requires valid IPv6 host");
         }
         co_await socket->async_connect(asio_tcp::endpoint{address, remote.port},
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      } else {
         const auto service = std::to_string(remote.port);
         auto results = co_await resolver->async_resolve(remote.host, service,
                                                         boost::asio::redirect_error(boost::asio::use_awaitable, error));
         if (!error) {
            auto filtered = filter_results(std::move(results), remote.host_type);
            if (filtered.empty()) {
               error = boost::asio::error::host_not_found;
            } else {
               co_await boost::asio::async_connect(*socket, filtered,
                                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
            }
         }
      }

      if (error) {
         if (error == boost::asio::error::operation_aborted) {
            FCL_THROW_EXCEPTION(exceptions::canceled, "tcp connect canceled",
                                fcl::exception::ctx("host", remote.host),
                                fcl::exception::ctx("port", remote.port));
         }
         throw_connect_failed(remote, error);
      }

      configure_socket(*socket, tcp_options);
      co_return connection{std::move(*socket), tcp_options};
   }

   boost::asio::awaitable<transport::stream_connection> async_connect(transport::endpoint remote,
                                                                      transport::connect_options) override {
      auto tcp_connection = co_await async_connect_connection(std::move(remote));
      co_return std::move(tcp_connection).into_transport_stream();
   }

   void cancel() override {
      active.store(false, std::memory_order_release);
      const auto lock = std::scoped_lock{mutex};
      for (auto& value : resolvers) {
         if (auto resolver = value.lock()) {
            resolver->cancel();
         }
      }
      for (auto& value : sockets) {
         if (auto socket = value.lock()) {
            auto ignored = boost::system::error_code{};
            socket->cancel(ignored);
         }
      }
   }

   boost::asio::any_io_executor executor;
   options tcp_options;
   mutable std::mutex mutex;
   std::vector<std::weak_ptr<asio_tcp::socket>> sockets;
   std::vector<std::weak_ptr<asio_tcp::resolver>> resolvers;
   std::atomic_bool active = true;
};

connector::connector() = default;
connector::connector(boost::asio::any_io_executor executor, options tcp_options)
    : impl_(std::make_shared<impl>(std::move(executor), tcp_options)) {}
connector::~connector() = default;
connector::connector(connector&&) noexcept = default;
connector& connector::operator=(connector&&) noexcept = default;

bool connector::valid() const noexcept {
   return impl_ && impl_->valid();
}

boost::asio::awaitable<connection> connector::async_connect_connection(transport::endpoint remote,
                                                                       transport::connect_options) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connector");
   }
   co_return co_await impl_->async_connect_connection(std::move(remote));
}

boost::asio::awaitable<transport::stream_connection> connector::async_connect(transport::endpoint remote,
                                                                              transport::connect_options options) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connector");
   }
   co_return co_await impl_->async_connect(std::move(remote), options);
}

void connector::cancel() {
   if (valid()) {
      impl_->cancel();
   }
}

transport::stream_connector connector::as_transport() const {
   if (!valid()) {
      return {};
   }
   return transport::detail::stream_connector_access::make(impl_);
}

} // namespace fcl::tcp
