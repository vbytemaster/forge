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

struct middleware_response {
   fcl::http::status status = fcl::http::status::ok;
   std::vector<header_entry> headers;
   std::string body;
   std::string content_type = "text/plain";

   [[nodiscard]] static middleware_response text(fcl::http::status status_value,
                                                 std::string body_value,
                                                 std::string content_type_value = "text/plain") {
      return middleware_response{.status = status_value,
                                 .headers = {},
                                 .body = std::move(body_value),
                                 .content_type = std::move(content_type_value)};
   }

   void set_header(std::string name, std::string value) {
      for (auto& entry : headers) {
         if (header_name_equal(entry.name, name)) {
            entry.value = std::move(value);
            return;
         }
      }
      headers.push_back(header_entry{.name = std::move(name), .value = std::move(value)});
   }
};

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
