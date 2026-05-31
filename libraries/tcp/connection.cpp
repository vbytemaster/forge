module;

#include <fcl/exception/macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

module fcl.tcp.connection;

import fcl.transport.stream;

namespace fcl::tcp {
namespace {

using asio_tcp = boost::asio::ip::tcp;

[[nodiscard]] std::int64_t next_stream_id() noexcept {
   static auto next = std::atomic<std::int64_t>{1};
   return next.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void throw_invalid_options(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_options, std::move(message));
}

[[noreturn]] void throw_io_error(std::string message, const boost::system::error_code& error) {
   FCL_THROW_EXCEPTION(exceptions::io_error, std::move(message), fcl::exception::ctx("reason", error.message()));
}

[[noreturn]] void throw_read_write_error(const boost::system::error_code& error) {
   if (error == boost::asio::error::operation_aborted) {
      FCL_THROW_EXCEPTION(exceptions::canceled, "tcp connection operation canceled",
                          fcl::exception::ctx("reason", error.message()));
   }
   if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset ||
       error == boost::asio::error::broken_pipe) {
      FCL_THROW_EXCEPTION(exceptions::closed, "tcp connection closed", fcl::exception::ctx("reason", error.message()));
   }
   throw_io_error("tcp connection I/O failed", error);
}

void validate_options(const options& value) {
   if (value.read_chunk_size == 0) {
      throw_invalid_options("tcp read_chunk_size must be greater than zero");
   }
}

[[nodiscard]] transport::endpoint from_asio_endpoint(const asio_tcp::endpoint& endpoint) {
   const auto address = endpoint.address();
   return transport::endpoint{.host_type = address.is_v6() ? transport::endpoint::host_kind::ip6
                                                           : transport::endpoint::host_kind::ip4,
                              .protocol = transport::endpoint::protocol_kind::tcp,
                              .host = address.to_string(),
                              .port = endpoint.port()};
}

class socket_stream final : public transport::detail::stream_concept {
 public:
   socket_stream(std::shared_ptr<asio_tcp::socket> socket, options tcp_options, std::int64_t id)
       : socket_(std::move(socket)), options_(tcp_options), id_(id) {}

   [[nodiscard]] bool valid() const noexcept override {
      return socket_ && socket_->is_open();
   }

   [[nodiscard]] std::int64_t id() const noexcept override {
      return id_;
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp stream");
      }
      auto error = boost::system::error_code{};
      co_await boost::asio::async_write(*socket_, boost::asio::buffer(bytes),
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() override {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp stream");
      }
      auto out = std::vector<std::uint8_t>(options_.read_chunk_size);
      auto error = boost::system::error_code{};
      const auto size = co_await socket_->async_read_some(boost::asio::buffer(out),
                                                          boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<void> async_close() override {
      if (!socket_) {
         co_return;
      }
      auto ignored = boost::system::error_code{};
      socket_->shutdown(asio_tcp::socket::shutdown_both, ignored);
      socket_->close(ignored);
      co_return;
   }

 private:
   std::shared_ptr<asio_tcp::socket> socket_;
   options options_;
   std::int64_t id_ = -1;
};

[[nodiscard]] transport::stream make_stream(std::shared_ptr<asio_tcp::socket> socket, options tcp_options,
                                            std::int64_t id) {
   return transport::detail::stream_access::make(
       std::make_shared<socket_stream>(std::move(socket), tcp_options, id));
}

} // namespace

struct connection::impl final {
   impl(asio_tcp::socket socket_value, options tcp_options_value)
       : socket(std::make_shared<asio_tcp::socket>(std::move(socket_value))), tcp_options(tcp_options_value),
         id(next_stream_id()) {
      validate_options(tcp_options);
   }

   [[nodiscard]] bool valid() const noexcept {
      return socket && socket->is_open();
   }

   [[nodiscard]] transport::endpoint local_endpoint() const {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto error = boost::system::error_code{};
      const auto endpoint = socket->local_endpoint(error);
      if (error) {
         throw_io_error("failed to read tcp local endpoint", error);
      }
      return from_asio_endpoint(endpoint);
   }

   [[nodiscard]] transport::endpoint remote_endpoint() const {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto error = boost::system::error_code{};
      const auto endpoint = socket->remote_endpoint(error);
      if (error) {
         throw_io_error("failed to read tcp remote endpoint", error);
      }
      return from_asio_endpoint(endpoint);
   }

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto error = boost::system::error_code{};
      co_await boost::asio::async_write(*socket, boost::asio::buffer(bytes),
                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
   }

   boost::asio::awaitable<std::vector<std::uint8_t>> async_read() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto out = std::vector<std::uint8_t>(tcp_options.read_chunk_size);
      auto error = boost::system::error_code{};
      const auto size = co_await socket->async_read_some(boost::asio::buffer(out),
                                                        boost::asio::redirect_error(boost::asio::use_awaitable, error));
      if (error) {
         throw_read_write_error(error);
      }
      out.resize(size);
      co_return out;
   }

   boost::asio::awaitable<void> async_close() {
      if (!socket) {
         co_return;
      }
      auto ignored = boost::system::error_code{};
      socket->shutdown(asio_tcp::socket::shutdown_both, ignored);
      socket->close(ignored);
      co_return;
   }

   [[nodiscard]] transport::stream_connection into_transport_stream() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto local = local_endpoint();
      auto remote = remote_endpoint();
      auto stream = make_stream(socket, tcp_options, id);
      socket.reset();
      return transport::stream_connection{.local_endpoint = std::move(local),
                                          .remote_endpoint = std::move(remote),
                                          .stream = std::move(stream)};
   }

   [[nodiscard]] asio_tcp::socket release_socket() {
      if (!valid()) {
         FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
      }
      auto out = std::move(*socket);
      socket.reset();
      return out;
   }

   std::shared_ptr<asio_tcp::socket> socket;
   options tcp_options;
   std::int64_t id = -1;
};

connection::connection() = default;
connection::connection(boost::asio::ip::tcp::socket socket, options tcp_options)
    : impl_(std::make_shared<impl>(std::move(socket), tcp_options)) {}
connection::~connection() = default;
connection::connection(connection&&) noexcept = default;
connection& connection::operator=(connection&&) noexcept = default;

bool connection::valid() const noexcept {
   return impl_ && impl_->valid();
}

transport::endpoint connection::local_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->local_endpoint();
}

transport::endpoint connection::remote_endpoint() const {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   return impl_->remote_endpoint();
}

boost::asio::awaitable<void> connection::async_write(std::span<const std::uint8_t> bytes) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   co_await impl_->async_write(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> connection::async_read() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   co_return co_await impl_->async_read();
}

boost::asio::awaitable<void> connection::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->async_close();
}

transport::stream_connection connection::into_transport_stream() && {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   auto out = impl_->into_transport_stream();
   impl_.reset();
   return out;
}

boost::asio::ip::tcp::socket connection::release_socket() && {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid tcp connection");
   }
   auto out = impl_->release_socket();
   impl_.reset();
   return out;
}

} // namespace fcl::tcp
