module;

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>

export module forge.http.stream;

import forge.http.body;
import forge.http.route_context;
import forge.http.types;

namespace forge::http::detail {
constexpr std::string_view request_body_stream_header = "X-FORGE-Request-Body-Stream";
}

export namespace forge::http {

struct stream_request {
   route_context& context;
   body_reader body;
};

struct stream_response {
   using body_source = std::function<boost::asio::awaitable<std::optional<body_chunk>>()>;

   response head;
   body_source body;

   [[nodiscard]] static stream_response buffered(response response_value) {
      return stream_response{.head = std::move(response_value), .body = {}};
   }
};

struct streaming_response_options {
   std::string content_type = "application/octet-stream";
   stream_response::body_source body;
};

class streaming_response {
 public:
   streaming_response() = default;

   [[nodiscard]] static streaming_response from_source(streaming_response_options options) {
      auto result = streaming_response{};
      result.head_.version(11);
      result.head_.set(field::content_type, options.content_type);
      result.source_ = std::move(options.body);
      return result;
   }

   [[nodiscard]] static streaming_response from_body(response head, body_reader body) {
      auto result = streaming_response{};
      head.set(detail::request_body_stream_header, "1");
      result.head_ = std::move(head);
      result.reader_ = std::move(body);
      return result;
   }

   [[nodiscard]] status status_code() const noexcept {
      return head_.result();
   }

   [[nodiscard]] const response& head() const noexcept {
      return head_;
   }

   [[nodiscard]] std::string content_type() const {
      if (auto found = head_.find(field::content_type); found != head_.end()) {
         return std::string{found->value()};
      }
      return {};
   }

   [[nodiscard]] body_reader& body() noexcept {
      return reader_;
   }

   [[nodiscard]] stream_response materialize(const request& request_value, status success_status) && {
      if (source_) {
         head_.result(success_status);
         head_.version(request_value.version());
         head_.keep_alive(request_value.keep_alive());
         return stream_response{.head = std::move(head_), .body = std::move(source_)};
      }
      if (reader_.valid()) {
         head_.version(request_value.version());
         head_.keep_alive(request_value.keep_alive());
         auto reader = std::move(reader_);
         return stream_response{.head = std::move(head_),
                                .body =
                                   [reader = std::move(reader)]() mutable
                                      -> boost::asio::awaitable<std::optional<body_chunk>> {
                                      co_return co_await reader.async_read();
                                   }};
      }
      return stream_response::buffered(std::move(head_));
   }

 private:
   response head_{status::ok, 11};
   stream_response::body_source source_;
   body_reader reader_;
};

using stream_route_handler = std::function<boost::asio::awaitable<stream_response>(stream_request&)>;

} // namespace forge::http
