module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module fcl.tcp.connector;

export import fcl.tcp.exceptions;
export import fcl.tcp.options;
export import fcl.transport.connector;

export namespace fcl::tcp {

class connector {
 public:
   connector();
   explicit connector(boost::asio::any_io_executor executor, options tcp_options = {});
   ~connector();

   connector(connector&&) noexcept;
   connector& operator=(connector&&) noexcept;

   connector(const connector&) = delete;
   connector& operator=(const connector&) = delete;

   [[nodiscard]] bool valid() const noexcept;

   boost::asio::awaitable<transport::stream_connection>
   async_connect(transport::endpoint remote, transport::connect_options connect_options = {});
   void cancel();

   [[nodiscard]] transport::stream_connector as_transport() const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::tcp
