module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module fcl.stcp.listener;

export import fcl.stcp.connection;
export import fcl.tcp.listener;

export namespace fcl::stcp {

class listener {
 public:
   listener();
   listener(boost::asio::any_io_executor executor, transport::endpoint local, server_options tls_options,
            transport::listen_options listen_options = {}, tcp::options tcp_options = {});
   ~listener();

   listener(listener&&) noexcept;
   listener& operator=(listener&&) noexcept;

   listener(const listener&) = delete;
   listener& operator=(const listener&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] transport::endpoint local_endpoint() const;

   boost::asio::awaitable<connection> async_accept_connection();
   boost::asio::awaitable<transport::stream_connection> async_accept();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::stcp
