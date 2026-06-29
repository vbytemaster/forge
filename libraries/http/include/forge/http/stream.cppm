module;

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>

namespace forge::http::detail {
struct stream_server_access;
}

export module forge.http.stream;

import forge.http.body;
import forge.http.route_context;
import forge.http.types;

export namespace forge::http {

struct stream_response {
   class body_source {
    public:
      using callback_type = std::function<boost::asio::awaitable<std::optional<body_chunk>>()>;

      body_source() = default;
      body_source(callback_type callback) : callback_(std::move(callback)) {}

      template <typename Callback>
         requires(!std::same_as<std::remove_cvref_t<Callback>, body_source>)
      body_source(Callback&& callback) : callback_(std::forward<Callback>(callback)) {}

      [[nodiscard]] explicit operator bool() const noexcept {
         return static_cast<bool>(callback_);
      }

      boost::asio::awaitable<std::optional<body_chunk>> operator()() {
         if (!callback_) {
            co_return std::nullopt;
         }
         co_return co_await callback_();
      }

    private:
      friend class streaming_response;
      friend struct stream_request;
      friend struct detail::stream_server_access;

      body_source(callback_type callback, std::shared_ptr<const void> request_body_marker)
          : callback_(std::move(callback)), request_body_marker_(std::move(request_body_marker)) {}

      callback_type callback_;
      std::shared_ptr<const void> request_body_marker_;
   };

   response head;
   body_source body;

   [[nodiscard]] static stream_response buffered(response response_value) {
      return stream_response{.head = std::move(response_value), .body = {}};
   }
};

struct stream_request {
   route_context& context;
   body_reader body;

   stream_request(route_context& context_value, body_reader body_value)
       : context(context_value), body(std::move(body_value)) {}

   [[nodiscard]] stream_response::body_source
   response_body(stream_response::body_source::callback_type callback) const {
      return stream_response::body_source{std::move(callback), request_body_marker_};
   }

 private:
   friend struct detail::stream_server_access;

   stream_request(route_context& context_value, body_reader body_value, std::shared_ptr<const void> request_body_marker)
       : context(context_value), body(std::move(body_value)), request_body_marker_(std::move(request_body_marker)) {}

   std::shared_ptr<const void> request_body_marker_;
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
      return std::move(*this).materialize_impl(request_value, success_status, nullptr);
   }

   [[nodiscard]] stream_response materialize(const stream_request& request_value, status success_status) && {
      return std::move(*this).materialize_impl(request_value.context.request, success_status, &request_value);
   }

 private:
   [[nodiscard]] stream_response
   materialize_impl(const request& request_value, status success_status, const stream_request* stream_request_value) && {
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
         auto callback =
            stream_response::body_source::callback_type{[reader = std::move(reader)]() mutable
                                                           -> boost::asio::awaitable<std::optional<body_chunk>> {
               co_return co_await reader.async_read();
            }};
         auto body = stream_request_value != nullptr ? stream_request_value->response_body(std::move(callback))
                                                     : stream_response::body_source{std::move(callback)};
         return stream_response{.head = std::move(head_),
                                .body = std::move(body)};
      }
      return stream_response::buffered(std::move(head_));
   }

   response head_{status::ok, 11};
   stream_response::body_source source_;
   body_reader reader_;
};

using stream_route_handler = std::function<boost::asio::awaitable<stream_response>(stream_request&)>;

} // namespace forge::http
