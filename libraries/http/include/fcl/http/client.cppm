module;

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

export module fcl.http.client;

import fcl.asio.runtime;
import fcl.http.base_url;
import fcl.http.body;
import fcl.http.connection;
import fcl.http.types;

export namespace fcl::http {

class client {
 public:
   client(fcl::asio::runtime& runtime, base_url endpoint);
   ~client();

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   boost::asio::awaitable<response> async_request(fcl::http::request request_value, request_options options = {});
   boost::asio::awaitable<response> async_streaming_request(fcl::http::request request_value,
                                                            body_reader body,
                                                            request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(fcl::http::request request_value,
                                                                request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(fcl::http::request request_value,
                                                                body_reader body,
                                                                request_options options = {});
   boost::asio::awaitable<response> async_send(method verb, std::string_view path, std::string body = {},
                                               std::string_view content_type = "application/octet-stream",
                                               request_options options = {});
   boost::asio::awaitable<response> async_get(std::string_view path, request_options options = {});
   boost::asio::awaitable<response> async_post_json(std::string_view path, std::string body,
                                                    request_options options = {});
   [[nodiscard]] std::string make_target(std::string_view path) const;
   [[nodiscard]] connection_metrics metrics() const;

 private:
   base_url endpoint_;
   connection connection_;
};

} // namespace fcl::http
