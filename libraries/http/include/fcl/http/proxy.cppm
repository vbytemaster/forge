module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.http.proxy;

import fcl.api.connection;
import fcl.api.descriptor;
import fcl.api.error_projection;
export import fcl.api.handle;
import fcl.http.body;
import fcl.http.binding;
export import fcl.http.client;
import fcl.http.exceptions;
import fcl.http.file;
export import fcl.http.mapping;
import fcl.http.stream;
import fcl.http.types;
import fcl.json;
import fcl.reflect.reflect;

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

template <typename Request>
void apply_route_headers(request& target, const api_route& route, const Request& value) {
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_header<member_type>::value) {
            const auto matched = std::find_if(route.headers.begin(), route.headers.end(), [&](const api_field_binding& binding) {
               return binding.field == field_name;
            });
            if (matched != route.headers.end() && (value.*member).present) {
               target.set(matched->name, (value.*member).value);
            }
         }
      });
   }
}

template <typename Request>
std::optional<body_reader> take_body_stream(Request& value, const api_route& route) {
   auto result = std::optional<body_reader>{};
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_body_stream_v<member_type>) {
            if (!result.has_value() &&
                (!route.body_stream_field.has_value() || *route.body_stream_field == field_name)) {
               result = (value.*member).release_reader();
            }
         }
      });
   }
   return result;
}

template <typename Request>
request make_client_request(client& target, const api_route& route, Request& value) {
   auto request_value = request{};
   request_value.method(route.verb);
   request_value.target(target.make_target(render_route_target(route, value)));
   request_value.version(11);
   apply_route_headers(request_value, route, value);
   return request_value;
}

boost::asio::awaitable<std::string> read_all(body_reader& body) {
   co_return co_await body.async_read_all();
}

template <typename Request, typename Response>
boost::asio::awaitable<Response> call(client& target, const fcl::api::descriptor& descriptor,
                                      const api_route& route, Request value) {
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = std::optional<body_reader>{};
      if constexpr (detail::request_needs_stream_v<Request>) {
         body = take_body_stream(value, route);
      } else if (uses_request_body(route.verb)) {
         auto encoded = fcl::json::write(value);
         if (!encoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request cannot be encoded as JSON");
         }
         request_value.body() = std::move(encoded.text);
         request_value.set(field::content_type, "application/json");
         request_value.prepare_payload();
      }

      auto response_value = body.has_value()
         ? co_await target.async_stream_request(std::move(request_value), std::move(*body))
         : co_await target.async_stream_request(std::move(request_value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_all(response_value.body);
         auto error = parse_error_payload(response_value.head);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else if constexpr (detail::is_bytes_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      if (uses_request_body(route.verb)) {
         auto encoded = fcl::json::write(value);
         if (!encoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request cannot be encoded as JSON");
         }
         request_value.body() = std::move(encoded.text);
         request_value.set(field::content_type, "application/json");
         request_value.prepare_payload();
      }
      auto response_value = co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = parse_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      auto bytes = std::vector<std::byte>(response_value.body().size());
      if (!bytes.empty()) {
         std::memcpy(bytes.data(), response_value.body().data(), response_value.body().size());
      }
      auto content_type = std::string{};
      if (auto iterator = response_value.find(field::content_type); iterator != response_value.end()) {
         content_type = std::string{iterator->value()};
      }
      co_return Response{
         .bytes = std::move(bytes),
         .content_type = content_type.empty() ? std::string{"application/octet-stream"} : std::move(content_type),
         .status_code = response_value.result(),
      };
   } else if constexpr (detail::is_empty_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      if (uses_request_body(route.verb)) {
         auto encoded = fcl::json::write(value);
         if (!encoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request cannot be encoded as JSON");
         }
         request_value.body() = std::move(encoded.text);
         request_value.set(field::content_type, "application/json");
         request_value.prepare_payload();
      }
      auto response_value = co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = parse_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      co_return Response{.status_code = response_value.result()};
   } else {
      auto request_value = make_client_request(target, route, value);
      if (uses_request_body(route.verb)) {
         if constexpr (detail::request_needs_stream_v<Request>) {
            auto body = take_body_stream(value, route);
            if (!body.has_value()) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                   "HTTP API streaming request body is not bound");
            }
            auto response_value = co_await target.async_streaming_request(std::move(request_value), std::move(*body));
            if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
               auto error = parse_error_payload(response_value);
               fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
            }
            auto decoded =
               fcl::json::read<Response>(response_value.body(),
                                          fcl::json::read_options{.source_name = "http.response",
                                                                  .unknown_fields =
                                                                     fcl::json::unknown_field_policy::error});
            if (!decoded.ok()) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response JSON is invalid");
            }
            co_return std::move(decoded.value);
         } else {
            auto encoded = fcl::json::write(value);
            if (!encoded.ok()) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request cannot be encoded as JSON");
            }
            request_value.body() = std::move(encoded.text);
            request_value.set(field::content_type, "application/json");
            request_value.prepare_payload();
         }
      }
      auto response_value = co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = parse_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      auto decoded = fcl::json::read<Response>(response_value.body(),
                                               fcl::json::read_options{.source_name = "http.response",
                                                                       .unknown_fields =
                                                                          fcl::json::unknown_field_policy::error});
      if (!decoded.ok()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response JSON is invalid");
      }
      co_return std::move(decoded.value);
   }
}

} // namespace detail

template <typename Interface>
boost::asio::awaitable<fcl::api::handle<Interface>> remote(client& value) {
   co_return fcl::api::handle<Interface>{std::make_shared<proxy<Interface>>(value)};
}

} // namespace fcl::http
