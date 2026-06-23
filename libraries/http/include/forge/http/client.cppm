module;

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

export module forge.http.client;

import forge.asio.runtime;
import forge.http.base_url;
import forge.http.body;
import forge.http.connection;
import forge.http.types;

export namespace forge::http {

class client {
 public:
   client(forge::asio::runtime& runtime, base_url endpoint);
   ~client();

   client(const client&) = delete;
   client& operator=(const client&) = delete;

   boost::asio::awaitable<response> async_request(forge::http::request request_value, request_options options = {});
   boost::asio::awaitable<response> async_streaming_request(forge::http::request request_value,
                                                            body_reader body,
                                                            request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(forge::http::request request_value,
                                                                request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(forge::http::request request_value,
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

} // namespace forge::http
