module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module forge.stcp.connector;

export import forge.stcp.connection;
export import forge.tcp.connector;

export namespace forge::stcp {

class connector {
 public:
   connector();
   connector(boost::asio::any_io_executor executor, client_options tls_options, tcp::options tcp_options = {});
   ~connector();

   connector(connector&&) noexcept;
   connector& operator=(connector&&) noexcept;

   connector(const connector&) = delete;
   connector& operator=(const connector&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<connection> async_connect_connection(
       transport::endpoint remote, transport::connect_options connect_options = {});
   boost::asio::awaitable<transport::stream_connection> async_connect(
       transport::endpoint remote, transport::connect_options connect_options = {});
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::stcp
