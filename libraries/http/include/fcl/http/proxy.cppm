module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <tuple>
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

[[nodiscard]] inline std::string default_header_name(std::string_view field_name) {
   auto output = std::string{};
   output.reserve(field_name.size());
   for (const auto character : field_name) {
      output.push_back(character == '_' ? '-' : character);
   }
   return output;
}

[[nodiscard]] inline std::string route_header_name(const api_route& route, std::string_view field_name) {
   const auto matched = std::find_if(route.headers.begin(), route.headers.end(), [&](const api_field_binding& binding) {
      return binding.field == field_name;
   });
   if (matched != route.headers.end()) {
      return matched->name;
   }
   return default_header_name(field_name);
}

template <typename Request>
void apply_route_headers(request& target, const api_route& route, const Request& value) {
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_header<member_type>::value) {
            const auto& header = value.*member;
            if (header.present) {
               auto encoded = field_to_text(header.value);
               if (!encoded.has_value()) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                      "HTTP API header value cannot be encoded as text");
               }
               target.set(route_header_name(route, field_name), std::move(*encoded));
            }
         }
      });
   }
}

template <typename Tuple>
[[nodiscard]] std::optional<std::string> tuple_argument_text(const Tuple& arguments,
                                                            const std::vector<std::string>& names,
                                                            std::string_view name) {
   auto result = std::optional<std::string>{};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if (result.has_value() || Index >= names.size() || names[Index] != name) {
             return;
          }
          const auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_header<argument_type>::value ||
                        detail::is_query<argument_type>::value ||
                        detail::is_cookie<argument_type>::value ||
                        detail::is_body<argument_type>::value ||
                        detail::is_form<argument_type>::value ||
                        detail::is_form_field<argument_type>::value) {
             if (argument.present) {
                result = field_to_text(argument.value);
             }
          } else if constexpr (!detail::is_body_stream_v<argument_type> &&
                               !detail::is_body_bytes_v<argument_type> &&
                               !detail::is_upload_file_v<argument_type>) {
             result = field_to_text(argument);
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return result;
}

template <typename Tuple>
[[nodiscard]] std::string require_tuple_argument_text(const Tuple& arguments,
                                                      const std::vector<std::string>& names,
                                                      std::string_view name) {
   auto value = tuple_argument_text(arguments, names, name);
   if (!value.has_value()) {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API positional argument is not available",
                          fcl::exceptions::ctx("field", std::string{name}));
   }
   return *value;
}

template <typename Tuple>
struct rendered_route_target {
   std::string target;
   std::array<bool, std::tuple_size_v<Tuple>> consumed{};
};

template <std::size_t Size>
void mark_argument_consumed(std::array<bool, Size>& consumed,
                            const std::vector<std::string>& names,
                            std::string_view name) {
   for (auto index = std::size_t{0}; index != consumed.size() && index != names.size(); ++index) {
      if (names[index] == name) {
         consumed[index] = true;
         return;
      }
   }
}

[[nodiscard]] inline std::string route_query_name(const api_route& route, std::string_view field_name) {
   const auto parsed = parse_route_template(route.target);
   const auto matched = std::find_if(parsed.query.begin(), parsed.query.end(), [&](const api_field_binding& binding) {
      return binding.field == field_name;
   });
   if (matched != parsed.query.end()) {
      return matched->name;
   }
   return std::string{field_name};
}

template <typename Tuple>
[[nodiscard]] rendered_route_target<Tuple> render_route_target(const api_route& route,
                                                              const Tuple& arguments,
                                                              const std::vector<std::string>& names) {
   auto result = rendered_route_target<Tuple>{};
   auto output = std::string{};
   output.reserve(route.target.size() + 32U);

   for (auto index = std::size_t{0}; index != route.target.size();) {
      const auto current = route.target[index];
      if (current == ':') {
         auto end = index + 1U;
         while (end != route.target.size()) {
            const auto value = route.target[end];
            if (std::isalnum(static_cast<unsigned char>(value)) == 0 && value != '_') {
               break;
            }
            ++end;
         }
         if (end == index + 1U) {
            output.push_back(current);
            ++index;
            continue;
         }
         const auto field_name = std::string_view{route.target}.substr(index + 1U, end - index - 1U);
         output += percent_encode(require_tuple_argument_text(arguments, names, field_name));
         mark_argument_consumed(result.consumed, names, field_name);
         index = end;
         continue;
      }
      if (current == '{') {
         const auto end = route.target.find('}', index + 1U);
         if (end != std::string::npos) {
            const auto field_name = std::string_view{route.target}.substr(index + 1U, end - index - 1U);
            output += percent_encode(require_tuple_argument_text(arguments, names, field_name));
            mark_argument_consumed(result.consumed, names, field_name);
            index = end + 1U;
            continue;
         }
      }
      output.push_back(current);
      ++index;
   }
   result.target = std::move(output);
   return result;
}

template <typename Tuple>
void append_unconsumed_query_arguments(std::string& target,
                                       const api_route& route,
                                       const Tuple& arguments,
                                       const std::vector<std::string>& names,
                                       const std::array<bool, std::tuple_size_v<Tuple>>& consumed) {
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if constexpr (Index < std::tuple_size_v<Tuple>) {
             if (Index >= names.size() || consumed[Index]) {
                return;
             }
             const auto& argument = std::get<Index>(arguments);
             using argument_type = std::remove_cvref_t<decltype(argument)>;
             if constexpr (detail::is_query<argument_type>::value) {
                if (!argument.present) {
                   return;
                }
                auto encoded = field_to_text(argument.value);
                if (!encoded.has_value()) {
                   FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                       "HTTP API query value cannot be encoded as text");
                }
                target.push_back(target.find('?') == std::string::npos ? '?' : '&');
                target += percent_encode(route_query_name(route, names[Index]));
                target.push_back('=');
                target += percent_encode(*encoded);
             }
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Tuple>
void apply_route_headers(request& target,
                         const api_route& route,
                         const Tuple& arguments,
                         const std::vector<std::string>& names) {
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if constexpr (Index < std::tuple_size_v<Tuple>) {
             const auto& argument = std::get<Index>(arguments);
             using argument_type = std::remove_cvref_t<decltype(argument)>;
             if constexpr (detail::is_header<argument_type>::value) {
                if (Index >= names.size()) {
                   FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                       "HTTP API positional header name is missing");
                }
                if (argument.present) {
                   auto encoded = field_to_text(argument.value);
                   if (!encoded.has_value()) {
                      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                          "HTTP API header value cannot be encoded as text");
                   }
                   target.set(route_header_name(route, names[Index]), std::move(*encoded));
                }
             }
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Tuple>
void apply_route_cookies(request& target,
                         const Tuple& arguments,
                         const std::vector<std::string>& names) {
   auto cookie = std::string{};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if constexpr (Index < std::tuple_size_v<Tuple>) {
             const auto& argument = std::get<Index>(arguments);
             using argument_type = std::remove_cvref_t<decltype(argument)>;
             if constexpr (detail::is_cookie<argument_type>::value) {
                if (Index >= names.size()) {
                   FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                       "HTTP API positional cookie name is missing");
                }
                if (argument.present) {
                   auto encoded = field_to_text(argument.value);
                   if (!encoded.has_value()) {
                      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                          "HTTP API cookie value cannot be encoded as text");
                   }
                   if (!cookie.empty()) {
                      cookie += "; ";
                   }
                   cookie += names[Index];
                   cookie += '=';
                   cookie += *encoded;
                }
             }
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});

   if (!cookie.empty()) {
      target.set(field::cookie, std::move(cookie));
   }
}

template <typename T>
inline constexpr auto is_http_parameter_v =
   is_header<std::remove_cvref_t<T>>::value ||
   is_query<std::remove_cvref_t<T>>::value ||
   is_cookie<std::remove_cvref_t<T>>::value ||
   is_body<std::remove_cvref_t<T>>::value ||
   is_form<std::remove_cvref_t<T>>::value ||
   is_form_field<std::remove_cvref_t<T>>::value ||
   is_body_stream_v<std::remove_cvref_t<T>> ||
   is_body_bytes_v<std::remove_cvref_t<T>> ||
   is_upload_file_v<std::remove_cvref_t<T>>;

template <typename Tuple>
std::optional<std::string> positional_json_body(const Tuple& arguments,
                                                const std::array<bool, std::tuple_size_v<Tuple>>& consumed) {
   auto result = std::optional<std::string>{};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          const auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_body<argument_type>::value) {
             if (!argument.present) {
                return;
             }
             if (result.has_value()) {
                FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                    "HTTP API request has multiple body parameters");
             }
             auto encoded = fcl::json::write(argument.value);
             if (!encoded.ok()) {
                FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                    "HTTP API request body cannot be encoded as JSON");
             }
             result = std::move(encoded.text);
          } else if constexpr (fcl::reflect::is_described_object_v<argument_type> &&
                               !detail::request_needs_stream_v<argument_type> &&
                               !is_http_parameter_v<argument_type>) {
             if (consumed[Index]) {
                return;
             }
             if (result.has_value()) {
                FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                    "HTTP API request has multiple positional body candidates");
             }
             auto encoded = fcl::json::write(argument);
             if (!encoded.ok()) {
                FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                    "HTTP API request body cannot be encoded as JSON");
             }
             result = std::move(encoded.text);
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return result;
}

template <typename Tuple>
std::size_t positional_plain_json_body_candidate_count(const std::array<bool, std::tuple_size_v<Tuple>>& consumed) {
   auto count = std::size_t{0};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          using argument_type = std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>;
          if constexpr (fcl::reflect::is_described_object_v<argument_type> &&
                        !detail::request_needs_stream_v<argument_type> &&
                        !is_http_parameter_v<argument_type>) {
             if (!consumed[Index]) {
                ++count;
             }
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return count;
}

template <typename Tuple>
consteval std::size_t positional_explicit_body_source_count() {
   auto count = std::size_t{0};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          using argument_type = std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>;
          if constexpr (detail::is_body<argument_type>::value ||
                        detail::is_body_stream_v<argument_type> ||
                        detail::is_body_bytes_v<argument_type>) {
             ++count;
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return count;
}

template <typename Tuple>
std::optional<body_reader> take_positional_body_stream(Tuple& arguments) {
   auto result = std::optional<body_reader>{};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if (result.has_value()) {
             return;
          }
          auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_body_stream_v<argument_type>) {
             result = argument.release_reader();
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return result;
}

template <typename Tuple>
std::optional<std::string> positional_body_bytes(Tuple& arguments) {
   auto result = std::optional<std::string>{};
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          if (result.has_value()) {
             return;
          }
          auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_body_bytes_v<argument_type>) {
             auto bytes = std::move(argument.bytes);
             result.emplace();
             result->resize(bytes.size());
             if (!bytes.empty()) {
                std::memcpy(result->data(), bytes.data(), bytes.size());
             }
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   return result;
}

template <typename Tuple>
void reject_unsupported_positional_client_body(const Tuple& arguments) {
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          const auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_form<argument_type>::value ||
                        detail::is_form_field<argument_type>::value ||
                        detail::is_upload_file_v<argument_type>) {
             static_cast<void>(argument);
             FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                 "HTTP positional form and upload client binding is not implemented");
          }
       }()),
       ...);
   }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Tuple>
std::optional<body_reader> bind_positional_request_body(request& target,
                                                        const api_route& route,
                                                        Tuple& arguments,
                                                        const std::array<bool, std::tuple_size_v<Tuple>>& consumed) {
   if (!uses_request_body(route.verb)) {
      return std::nullopt;
   }
   reject_unsupported_positional_client_body(arguments);
   constexpr auto explicit_body_sources = positional_explicit_body_source_count<Tuple>();
   if constexpr (explicit_body_sources > 1U) {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request has multiple body parameters");
   }
   const auto plain_body_candidates = positional_plain_json_body_candidate_count<Tuple>(consumed);
   if constexpr (explicit_body_sources > 0U) {
      if (plain_body_candidates > 0U) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                             "HTTP API request has explicit and inferred body parameters");
      }
   }
   auto stream_body = take_positional_body_stream(arguments);
   if (stream_body.has_value()) {
      return stream_body;
   }
   auto bytes_body = positional_body_bytes(arguments);
   if (bytes_body.has_value()) {
      target.body() = std::move(*bytes_body);
      target.prepare_payload();
      return std::nullopt;
   }
   auto json_body = positional_json_body(arguments, consumed);
   if (json_body.has_value()) {
      target.body() = std::move(*json_body);
      target.set(field::content_type, "application/json");
      target.prepare_payload();
   }
   return std::nullopt;
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

template <typename Tuple>
struct positional_client_request {
   request value;
   std::array<bool, std::tuple_size_v<Tuple>> consumed{};
};

template <typename Request>
request make_client_request(client& target, const api_route& route, Request& value) {
   auto request_value = request{};
   request_value.method(route.verb);
   request_value.target(target.make_target(render_route_target(route, value)));
   request_value.version(11);
   apply_route_headers(request_value, route, value);
   return request_value;
}

template <typename Tuple>
positional_client_request<Tuple> make_client_request(client& target,
                                                     const api_route& route,
                                                     const Tuple& arguments,
                                                     const std::vector<std::string>& names) {
   auto output = positional_client_request<Tuple>{};
   auto rendered = render_route_target(route, arguments, names);
   append_unconsumed_query_arguments(rendered.target, route, arguments, names, rendered.consumed);

   auto request_value = request{};
   request_value.method(route.verb);
   request_value.target(target.make_target(rendered.target));
   request_value.version(11);
   apply_route_headers(request_value, route, arguments, names);
   apply_route_cookies(request_value, arguments, names);
   output.value = std::move(request_value);
   output.consumed = rendered.consumed;
   return output;
}

inline constexpr auto max_stream_error_body_bytes = std::uint64_t{64U * 1024U};

boost::asio::awaitable<std::string> read_bounded_error_body(body_reader& body) {
   auto output = std::string{};
   while (auto chunk = co_await body.async_read()) {
      if (chunk->bytes.size() > max_stream_error_body_bytes - output.size()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::payload_too_large,
                             "HTTP API error response body exceeds the streaming client limit");
      }
      output.append(reinterpret_cast<const char*>(chunk->bytes.data()), chunk->bytes.size());
   }
   co_return output;
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
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
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

template <typename Tuple, typename Response>
boost::asio::awaitable<Response> call_arguments(client& target,
                                                const fcl::api::descriptor& descriptor,
                                                const api_route& route,
                                                Tuple value,
                                                const std::vector<std::string>& argument_names) {
   auto request_parts = make_client_request(target, route, value, argument_names);
   auto request_body = bind_positional_request_body(request_parts.value, route, value, request_parts.consumed);
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto response_value = request_body.has_value()
         ? co_await target.async_stream_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_stream_request(std::move(request_parts.value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = parse_error_payload(response_value.head);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else {
      auto response_value = request_body.has_value()
         ? co_await target.async_streaming_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_request(std::move(request_parts.value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = parse_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (detail::is_bytes_response_v<Response>) {
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
         co_return Response{.status_code = response_value.result()};
      } else {
         auto decoded = fcl::json::read<Response>(
            response_value.body(),
            fcl::json::read_options{.source_name = "http.response",
                                    .unknown_fields = fcl::json::unknown_field_policy::error});
         if (!decoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response JSON is invalid");
         }
         co_return std::move(decoded.value);
      }
   }
}

struct route_call {
   std::string method;
   std::function<boost::asio::awaitable<fcl::api::response>(client&, const fcl::api::descriptor&, fcl::api::request)>
      handler;
   std::function<boost::asio::awaitable<void>(client&,
                                              const fcl::api::descriptor&,
                                              fcl::api::request,
                                              std::type_index,
                                              void*,
                                              std::type_index,
                                              void*)>
      typed_handler;
};

class route_invoker final : public fcl::api::remote_invoker {
 public:
   route_invoker(client& target, fcl::api::descriptor descriptor, std::vector<route_call> routes)
       : target_{&target}, descriptor_{std::move(descriptor)}, routes_{std::move(routes)} {}

   boost::asio::awaitable<fcl::api::response> async_call(fcl::api::request value) override {
      const auto route =
         std::find_if(routes_.begin(), routes_.end(), [&](const route_call& candidate) {
            return candidate.method == value.method;
         });
      if (route == routes_.end()) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found, "HTTP API route is not declared",
                             fcl::exceptions::ctx("method", value.method));
      }
      co_return co_await route->handler(*target_, descriptor_, std::move(value));
   }

   bool supports_typed_arguments() const noexcept override {
      return true;
   }

   boost::asio::awaitable<void> async_call_arguments(fcl::api::request value,
                                                     std::type_index argument_tuple_type,
                                                     void* argument_tuple,
                                                     std::type_index response_type,
                                                     void* response_storage) override {
      const auto route =
         std::find_if(routes_.begin(), routes_.end(), [&](const route_call& candidate) {
            return candidate.method == value.method;
         });
      if (route == routes_.end()) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found, "HTTP API route is not declared",
                             fcl::exceptions::ctx("method", value.method));
      }
      co_await route->typed_handler(*target_, descriptor_, std::move(value), argument_tuple_type, argument_tuple,
                                    response_type, response_storage);
   }

 private:
   client* target_;
   fcl::api::descriptor descriptor_;
   std::vector<route_call> routes_;
};

[[nodiscard]] inline std::vector<std::string> argument_names_for(const fcl::api::descriptor& descriptor,
                                                                 std::string_view method) {
   const auto* value = fcl::api::find_method(descriptor, method);
   if (value == nullptr) {
      FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found, "HTTP API route method is not declared",
                          fcl::exceptions::ctx("method", std::string{method}));
   }
   return value->argument_names;
}

template <auto Method, typename Request, typename Response>
route_call make_route_call(api_route route) {
   return route_call{
      .method = route.method_name,
      .handler = [route](client& target,
                         const fcl::api::descriptor& descriptor,
                         fcl::api::request value) -> boost::asio::awaitable<fcl::api::response> {
         auto output = fcl::api::response{
            .api = value.api,
            .method = value.method,
            .codec = value.codec,
         };
         if constexpr (fcl::api::method_argument_count_v<Method> == 1U) {
            if constexpr (is_http_parameter_v<Request>) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error,
                                   "HTTP parameter methods require typed HTTP argument invocation");
            } else {
               auto request_value = fcl::api::unpack_body<Request>(value.body);
               auto response_value =
                  co_await call<Request, Response>(target, descriptor, route, std::move(request_value));
               output.body = fcl::api::pack_body(response_value);
            }
         } else {
            using argument_tuple = fcl::api::method_argument_tuple_t<Method>;
            auto arguments = fcl::api::unpack_body<argument_tuple>(value.body);
            auto response_value = co_await call_arguments<argument_tuple, Response>(
               target, descriptor, route, std::move(arguments), argument_names_for(descriptor, route.method_name));
            output.body = fcl::api::pack_body(response_value);
         }
         co_return output;
      },
      .typed_handler =
         [route = std::move(route)](client& target,
                                    const fcl::api::descriptor& descriptor,
                                    fcl::api::request value,
                                    std::type_index argument_tuple_type,
                                    void* argument_tuple,
                                    std::type_index response_type,
                                    void* response_storage) -> boost::asio::awaitable<void> {
         using argument_tuple_t = fcl::api::method_argument_tuple_t<Method>;
         if (argument_tuple_type != typeid(argument_tuple_t) || response_type != typeid(Response)) {
            FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error,
                                "HTTP API typed argument invocation has incompatible storage");
         }
         auto& arguments = *static_cast<argument_tuple_t*>(argument_tuple);
         auto& output = *static_cast<std::optional<Response>*>(response_storage);
         output.emplace(co_await call_arguments<argument_tuple_t, Response>(
            target, descriptor, route, std::move(arguments), argument_names_for(descriptor, value.method)));
      },
   };
}

inline std::shared_ptr<fcl::api::remote_invoker> make_route_invoker(client& target,
                                                                    fcl::api::descriptor descriptor,
                                                                    std::vector<route_call> routes) {
   return std::make_shared<route_invoker>(target, std::move(descriptor), std::move(routes));
}

template <auto Method, typename Request, typename Response>
inline constexpr auto route_can_use_api_proxy_v =
   fcl::api::method_argument_count_v<Method> != 1U ||
   (!detail::request_needs_stream_v<Request> &&
   !detail::response_needs_stream_v<Response> &&
   !detail::is_bytes_response_v<Response> &&
   !detail::is_empty_response_v<Response>);

} // namespace detail

template <typename Interface>
boost::asio::awaitable<fcl::api::handle<Interface>> remote(client& value) {
   if constexpr (http_api_traits<Interface>::use_api_proxy) {
      co_return fcl::api::handle<Interface>{
         std::make_shared<fcl::api::proxy<Interface>>(
            http_api_traits<Interface>::make_invoker(value),
            Interface::ref())};
   } else {
      co_return fcl::api::handle<Interface>{std::make_shared<proxy<Interface>>(value)};
   }
}

} // namespace fcl::http
