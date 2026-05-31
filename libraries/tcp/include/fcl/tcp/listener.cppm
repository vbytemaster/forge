module;

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

export module fcl.tcp.listener;

export import fcl.tcp.connector;
export import fcl.transport.listener;

export namespace fcl::tcp {

class listener {
 public:
   listener();
   listener(boost::asio::any_io_executor executor, transport::endpoint local,
            transport::listen_options listen_options = {}, options tcp_options = {});
   ~listener();

   listener(listener&&) noexcept;
   listener& operator=(listener&&) noexcept;

   listener(const listener&) = delete;
   listener& operator=(const listener&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] transport::endpoint local_endpoint() const;

   boost::asio::awaitable<transport::stream_connection> async_accept();
   boost::asio::awaitable<void> async_close();
   void cancel();

   [[nodiscard]] transport::stream_listener as_transport() const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::tcp
