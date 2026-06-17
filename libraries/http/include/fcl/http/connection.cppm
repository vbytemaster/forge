module;

#include <chrono>
#include <memory>
#include <cstdint>
#include <string>

#include <boost/asio/awaitable.hpp>

export module fcl.http.connection;

import fcl.asio.runtime;
import fcl.http.base_url;
import fcl.http.body;
import fcl.http.types;

export namespace fcl::http {

struct request_options {
   std::chrono::milliseconds timeout{30'000};
   bool retry_idempotent = false;
   std::uint32_t max_retries = 0;
   std::chrono::milliseconds retry_backoff{50};
};

struct connection_metrics {
   std::uint64_t queued_requests = 0;
   std::uint64_t started_requests = 0;
   std::uint64_t completed_requests = 0;
   std::uint64_t failed_requests = 0;
   std::uint64_t retry_attempts = 0;
   std::uint64_t reconnects = 0;
   std::uint64_t timeouts = 0;
   std::uint64_t cancellations = 0;
   std::uint64_t status_1xx = 0;
   std::uint64_t status_2xx = 0;
   std::uint64_t status_3xx = 0;
   std::uint64_t status_4xx = 0;
   std::uint64_t status_5xx = 0;
   std::size_t queue_depth = 0;
};

struct response_stream {
   response head;
   body_reader body;
};

class connection {
 public:
   connection(fcl::asio::runtime& runtime, base_url endpoint);
   ~connection();

   connection(const connection&) = delete;
   connection& operator=(const connection&) = delete;

   boost::asio::awaitable<response> async_request(fcl::http::request request_value, request_options options = {});
   boost::asio::awaitable<response> async_streaming_request(fcl::http::request request_value,
                                                            body_reader body,
                                                            request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(fcl::http::request request_value,
                                                                request_options options = {});
   boost::asio::awaitable<response_stream> async_stream_request(fcl::http::request request_value,
                                                                body_reader body,
                                                                request_options options = {});
   [[nodiscard]] connection_metrics metrics() const;

 private:
   struct impl;

   std::unique_ptr<impl> impl_;
};

} // namespace fcl::http
