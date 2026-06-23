module;

#include <coroutine>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/beast/http.hpp>

module forge.http.client;

namespace forge::http {
namespace {

request make_request(method method_value, const base_url& endpoint, std::string_view path, std::string body = {},
                     std::string_view content_type = {}) {
   auto request_value = request{};
   request_value.method(method_value);
   request_value.target(endpoint.make_target(path));
   request_value.version(11);
   request_value.body() = std::move(body);
   if (!request_value.body().empty()) {
      request_value.set(field::content_type, content_type.empty() ? std::string_view{"application/octet-stream"}
                                                                  : content_type);
      request_value.prepare_payload();
   }
   return request_value;
}

} // namespace

client::client(forge::asio::runtime& runtime, base_url endpoint)
    : endpoint_(std::move(endpoint)), connection_(runtime, endpoint_) {}

client::~client() = default;

boost::asio::awaitable<response> client::async_request(forge::http::request request_value, request_options options) {
   co_return co_await connection_.async_request(std::move(request_value), options);
}

boost::asio::awaitable<response> client::async_streaming_request(forge::http::request request_value,
                                                                 body_reader body,
                                                                 request_options options) {
   co_return co_await connection_.async_streaming_request(std::move(request_value), std::move(body), options);
}

boost::asio::awaitable<response_stream> client::async_stream_request(forge::http::request request_value,
                                                                     request_options options) {
   co_return co_await connection_.async_stream_request(std::move(request_value), options);
}

boost::asio::awaitable<response_stream> client::async_stream_request(forge::http::request request_value,
                                                                     body_reader body,
                                                                     request_options options) {
   co_return co_await connection_.async_stream_request(std::move(request_value), std::move(body), options);
}

boost::asio::awaitable<response> client::async_send(method verb, std::string_view path, std::string body,
                                                    std::string_view content_type, request_options options) {
   co_return co_await async_request(make_request(verb, endpoint_, path, std::move(body), content_type), options);
}

boost::asio::awaitable<response> client::async_get(std::string_view path, request_options options) {
   co_return co_await async_request(make_request(method::get, endpoint_, path), options);
}

boost::asio::awaitable<response> client::async_post_json(std::string_view path, std::string body,
                                                         request_options options) {
   co_return co_await async_request(make_request(method::post, endpoint_, path, std::move(body), "application/json"),
                                    options);
}

std::string client::make_target(std::string_view path) const {
   return endpoint_.make_target(path);
}

connection_metrics client::metrics() const {
   return connection_.metrics();
}

} // namespace forge::http
