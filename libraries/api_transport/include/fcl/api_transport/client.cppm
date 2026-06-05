module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module fcl.api.transport.client;

export import fcl.api.transport.exceptions;
export import fcl.api.transport.options;
export import fcl.transport.stream;

export namespace fcl::api::transport {

class client {
 public:
   client();
   client(fcl::transport::stream stream, options value = {});
   ~client();

   client(client&&) noexcept;
   client& operator=(client&&) noexcept;

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<frame> async_call(frame request, call_options value = {});
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::api::transport
