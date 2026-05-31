module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module fcl.stcp.connector;

export import fcl.stcp.connection;
export import fcl.tcp.connector;

export namespace fcl::stcp {

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

} // namespace fcl::stcp
