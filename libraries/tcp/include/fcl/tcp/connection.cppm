module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

export module fcl.tcp.connection;

export import fcl.tcp.exceptions;
export import fcl.tcp.options;
export import fcl.transport.connector;

export namespace fcl::tcp {

class connection {
 public:
   connection();
   explicit connection(boost::asio::ip::tcp::socket socket, options tcp_options = {});
   ~connection();

   connection(connection&&) noexcept;
   connection& operator=(connection&&) noexcept;

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] transport::endpoint local_endpoint() const;
   [[nodiscard]] transport::endpoint remote_endpoint() const;

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read();
   boost::asio::awaitable<void> async_close();

   [[nodiscard]] transport::stream_connection into_transport_stream() &&;
   [[nodiscard]] boost::asio::ip::tcp::socket release_socket() &&;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::tcp
