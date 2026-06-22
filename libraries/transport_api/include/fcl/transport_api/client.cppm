module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

export module fcl.transport.api.client;

export import fcl.api.types;
export import fcl.transport.api.exceptions;
export import fcl.transport.api.options;
export import fcl.transport.stream;

export namespace fcl::transport::api {

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

   boost::asio::awaitable<fcl::api::frame> async_call(fcl::api::frame request, call_options value = {});
   boost::asio::awaitable<std::vector<fcl::api::frame>> async_call_stream(fcl::api::frame request,
                                                                          call_options value = {});
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::transport::api
