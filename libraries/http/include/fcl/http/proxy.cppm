module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

export module fcl.http.proxy;

import fcl.api.connection;
import fcl.api.descriptor;
import fcl.api.error_projection;
export import fcl.api.handle;
export import fcl.http.client;
import fcl.http.exceptions;
export import fcl.http.mapping;
import fcl.http.types;

export namespace fcl::http {

template <typename Interface> class proxy;

namespace detail {

[[nodiscard]] inline std::optional<std::string> json_string_field(std::string_view text, std::string_view field) {
   const auto needle = std::string{"\""} + std::string{field} + "\":\"";
   const auto start = text.find(needle);
   if (start == std::string_view::npos) {
      return std::nullopt;
   }
   auto index = start + needle.size();
   auto output = std::string{};
   while (index != text.size()) {
      const auto current = text[index++];
      if (current == '"') {
         return output;
      }
      if (current != '\\' || index == text.size()) {
         output.push_back(current);
         continue;
      }
      const auto escaped = text[index++];
      switch (escaped) {
      case '"':
      case '\\':
         output.push_back(escaped);
         break;
      case 'n':
         output.push_back('\n');
         break;
      case 'r':
         output.push_back('\r');
         break;
      case 't':
         output.push_back('\t');
         break;
      case 'u':
         if (index + 4U <= text.size() && text[index] == '0' && text[index + 1U] == '0') {
            auto value = 0U;
            for (auto offset = 2U; offset != 4U; ++offset) {
               const auto ch = text[index + offset];
               value <<= 4U;
               if (ch >= '0' && ch <= '9') {
                  value += static_cast<unsigned>(ch - '0');
               } else if (ch >= 'a' && ch <= 'f') {
                  value += static_cast<unsigned>(ch - 'a' + 10);
               } else if (ch >= 'A' && ch <= 'F') {
                  value += static_cast<unsigned>(ch - 'A' + 10);
               }
            }
            output.push_back(static_cast<char>(value));
            index += 4U;
         }
         break;
      default:
         output.push_back(escaped);
         break;
      }
   }
   return std::nullopt;
}

[[nodiscard]] inline bool json_bool_field(std::string_view text, std::string_view field) {
   const auto needle = std::string{"\""} + std::string{field} + "\":";
   const auto start = text.find(needle);
   if (start == std::string_view::npos) {
      return false;
   }
   const auto value = text.substr(start + needle.size());
   return value.starts_with("true");
}

[[nodiscard]] inline std::uint32_t json_identity_code(std::string_view text) {
   const auto needle = std::string{"\"code\":"};
   const auto start = text.rfind(needle);
   if (start == std::string_view::npos) {
      return static_cast<std::uint32_t>(fcl::api::exceptions::code::remote_internal);
   }
   auto index = start + needle.size();
   auto value = std::uint32_t{0};
   while (index != text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
      value = (value * 10U) + static_cast<std::uint32_t>(text[index] - '0');
      ++index;
   }
   return value;
}

[[nodiscard]] inline fcl::api::error_payload parse_error_payload(const response& value) {
   const auto body = std::string_view{value.body()};
   return fcl::api::error_payload{
       .error = json_string_field(body, "error").value_or("http_error"),
       .message = json_string_field(body, "message").value_or("HTTP API request failed"),
       .retryable = json_bool_field(body, "retryable"),
       .status_code = static_cast<fcl::api::status>(value.result_int()),
       .identity =
           {
               .category = json_string_field(body, "category").value_or("fcl.api"),
               .code = json_identity_code(body),
           },
   };
}

template <typename Request, typename Response>
boost::asio::awaitable<Response> call(client& target, const fcl::api::descriptor& descriptor,
                                      const api_route& route, Request value) {
   auto body = std::string{};
   auto content_type = std::string_view{};
   if (uses_request_body(route.verb)) {
      auto bytes = fcl::api::pack_body(value);
      body.assign(bytes.begin(), bytes.end());
      content_type = "application/octet-stream";
   }

   auto response_value =
       co_await target.async_send(route.verb, render_route_target(route, value), std::move(body), content_type);
   if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
      auto error = parse_error_payload(response_value);
      fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
   }

   auto bytes = fcl::api::bytes{response_value.body().begin(), response_value.body().end()};
   co_return fcl::api::unpack_body<Response>(bytes);
}

} // namespace detail

template <typename Interface>
boost::asio::awaitable<fcl::api::handle<Interface>> remote(client& value) {
   static_assert(fcl::api::remote_interface<Interface>, "Interface must opt in to fcl::api::surface::remote");
   co_return fcl::api::handle<Interface>{std::make_shared<proxy<Interface>>(value)};
}

} // namespace fcl::http
