module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module forge.tcp.connector;

export import forge.tcp.connection;
export import forge.tcp.exceptions;
export import forge.tcp.options;
export import forge.transport.connector;

export namespace forge::tcp {

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

   boost::asio::awaitable<connection> async_connect_connection(transport::endpoint remote,
                                                               transport::connect_options connect_options = {});
   boost::asio::awaitable<transport::stream_connection>
   async_connect(transport::endpoint remote, transport::connect_options connect_options = {});
   void cancel();

   [[nodiscard]] transport::stream_connector as_transport() const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::tcp
