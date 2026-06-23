module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

export module forge.transport.api.client;

export import forge.api.types;
export import forge.transport.api.exceptions;
export import forge.transport.api.options;
export import forge.transport.stream;

export namespace forge::transport::api {

class client {
 public:
   client();
   client(forge::transport::stream stream, options value = {});
   ~client();

   client(client&&) noexcept;
   client& operator=(client&&) noexcept;

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] const options& settings() const noexcept;

   boost::asio::awaitable<forge::api::frame> async_call(forge::api::frame request, call_options value = {});
   boost::asio::awaitable<std::vector<forge::api::frame>> async_call_stream(forge::api::frame request,
                                                                          call_options value = {});
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::transport::api
