module;

#include <boost/asio/awaitable.hpp>

#include <cctype>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module fcl.plugins.http_server.middleware;

import fcl.http.types;

export namespace fcl::plugins::http_server {

namespace detail {
struct middleware_bridge_access;
}

enum class middleware_phase {
   request_context = 1,
   security = 2,
   limits = 3,
   before_handler = 4,
   after_handler = 5,
   error = 6,
};

struct header_entry {
   std::string name;
   std::string value;
};

[[nodiscard]] inline bool header_name_equal(std::string_view lhs, std::string_view rhs) noexcept {
   if (lhs.size() != rhs.size()) {
      return false;
   }
   for (auto index = std::size_t{0}; index != lhs.size(); ++index) {
      const auto left = static_cast<unsigned char>(lhs[index]);
      const auto right = static_cast<unsigned char>(rhs[index]);
      if (std::tolower(left) != std::tolower(right)) {
         return false;
      }
   }
   return true;
}

struct middleware_request {
   std::string method;
   std::string target;
   std::string path;
   std::vector<header_entry> headers;

   [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const noexcept {
      for (const auto& entry : headers) {
         if (header_name_equal(entry.name, name)) {
            return std::string_view{entry.value};
         }
      }
      return std::nullopt;
   }
};

class middleware_response {
 public:
   [[nodiscard]] static middleware_response text(fcl::http::status status_value,
                                                 std::string body_value,
                                                 std::string content_type_value = "text/plain") {
      auto result = middleware_response{};
      result.status_ = status_value;
      result.body_ = std::move(body_value);
      result.content_type_ = std::move(content_type_value);
      return result;
   }

   [[nodiscard]] fcl::http::status status() const noexcept {
      return status_;
   }

   [[nodiscard]] const std::vector<header_entry>& headers() const noexcept {
      return headers_;
   }

   [[nodiscard]] const std::string& body() const noexcept {
      return body_;
   }

   [[nodiscard]] const std::string& content_type() const noexcept {
      return content_type_;
   }

   void set_header(std::string name, std::string value) {
      for (auto& entry : headers_) {
         if (header_name_equal(entry.name, name)) {
            entry.value = std::move(value);
            return;
         }
      }
      headers_.push_back(header_entry{.name = std::move(name), .value = std::move(value)});
   }

   void set_status(fcl::http::status value) noexcept {
      status_ = value;
      stream_token_.clear();
   }

   void set_body(std::string value) {
      body_ = std::move(value);
      stream_token_.clear();
   }

   void clear_body() {
      body_.clear();
      stream_token_.clear();
   }

   void set_content_type(std::string value) {
      content_type_ = std::move(value);
   }

 private:
   fcl::http::status status_ = fcl::http::status::ok;
   std::vector<header_entry> headers_;
   std::string body_;
   std::string content_type_ = "text/plain";
   std::string stream_token_;

   friend struct detail::middleware_bridge_access;
};

namespace detail {

struct middleware_bridge_access {
   static void set_status(middleware_response& value, fcl::http::status status) noexcept {
      value.status_ = status;
   }

   static void set_body(middleware_response& value, std::string body) {
      value.body_ = std::move(body);
   }

   [[nodiscard]] static std::string take_body(middleware_response& value) {
      return std::move(value.body_);
   }

   static void set_content_type(middleware_response& value, std::string content_type) {
      value.content_type_ = std::move(content_type);
   }

   static std::vector<header_entry>& headers(middleware_response& value) noexcept {
      return value.headers_;
   }

   static void set_stream_token(middleware_response& value, std::string token) {
      value.stream_token_ = std::move(token);
   }

   [[nodiscard]] static const std::string& stream_token(const middleware_response& value) noexcept {
      return value.stream_token_;
   }
};

} // namespace detail

using middleware_next = std::function<boost::asio::awaitable<middleware_response>()>;
using middleware_handler =
   std::function<boost::asio::awaitable<middleware_response>(const middleware_request&, middleware_next)>;

struct middleware_descriptor {
   std::string id;
   middleware_phase phase = middleware_phase::before_handler;
   int order = 0;
   std::string path_prefix = "/";
   middleware_handler handler;
};

} // namespace fcl::plugins::http_server
