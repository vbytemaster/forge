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
import fcl.api.types;
export import fcl.api.handle;
import fcl.http.body;
import fcl.http.binding;
export import fcl.http.client;
import fcl.http.exceptions;
import fcl.http.file;
export import fcl.http.mapping;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.json;
import fcl.reflect.reflect;

export namespace fcl::http {

template <typename Interface> class proxy;

namespace detail {

[[nodiscard]] inline fcl::api::error_payload decode_error_payload(const response& value) {
   auto decoded = fcl::json::read<fcl::api::error_payload>(
      value.body(), fcl::json::read_options{.source_name = "http.error",
                                            .unknown_fields = fcl::json::unknown_field_policy::ignore});
   if (decoded.ok()) {
      auto payload = std::move(decoded.value);
      payload.status_code = static_cast<fcl::api::status>(value.result_int());
      return payload;
   }
   return fcl::api::error_payload{
      .error = "http_error",
      .message = value.body().empty() ? "HTTP API request failed" : value.body(),
      .retryable = false,
      .status_code = static_cast<fcl::api::status>(value.result_int()),
      .identity =
         {
            .category = "fcl.api",
            .code = static_cast<std::uint32_t>(fcl::api::exceptions::code::remote_internal),
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

[[nodiscard]] inline std::string route_query_name(const api_route& route, std::string_view field_name);

[[nodiscard]] inline std::string route_form_name(const api_route& route, std::string_view field_name) {
   const auto matched = std::find_if(route.forms.begin(), route.forms.end(), [&](const api_field_binding& binding) {
      return binding.field == field_name;
   });
   if (matched != route.forms.end()) {
      return matched->name;
   }
   return std::string{field_name};
}

template <typename Request>
void apply_route_headers(request& target, const api_route& route, const Request& value) {
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_header<member_type>::value) {
            const auto& header = value.*member;
            if (header.present) {
               auto encoded = format_http_field(header.value);
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

template <typename Request>
void append_route_query_fields(std::string& target, const api_route& route, const Request& value) {
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      const auto parsed = parse_route_template(route.target);
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_query<member_type>::value) {
            const auto already_rendered =
               std::find_if(parsed.query.begin(), parsed.query.end(), [&](const api_field_binding& binding) {
                  return binding.field == field_name;
               }) != parsed.query.end();
            const auto& query = value.*member;
            if (!already_rendered && query.present) {
               auto encoded = format_http_field(query.value);
               if (!encoded.has_value()) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                      "HTTP API query value cannot be encoded as text");
               }
               target.push_back(target.find('?') == std::string::npos ? '?' : '&');
               target += encode_query_component(route_query_name(route, field_name));
               target.push_back('=');
               target += encode_query_component(*encoded);
            }
         }
      });
   }
}

template <typename Request>
void apply_route_cookies(request& target, const Request& value) {
   auto cookie = std::string{};
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_cookie<member_type>::value) {
            const auto& field = value.*member;
            if (field.present) {
               auto encoded = format_http_field(field.value);
               if (!encoded.has_value()) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                      "HTTP API cookie value cannot be encoded as text");
               }
               if (!cookie.empty()) {
                  cookie += "; ";
               }
               cookie += field_name;
               cookie += '=';
               cookie += *encoded;
            }
         }
      });
   }
   if (!cookie.empty()) {
      target.set(field::cookie, std::move(cookie));
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
                result = format_http_field(argument.value);
             }
          } else if constexpr (!detail::is_body_stream_v<argument_type> &&
                               !detail::is_body_bytes_v<argument_type> &&
                               !detail::is_upload_file_v<argument_type>) {
             result = format_http_field(argument);
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
         output += encode_path_segment(require_tuple_argument_text(arguments, names, field_name));
         mark_argument_consumed(result.consumed, names, field_name);
         index = end;
         continue;
      }
      if (current == '{') {
         const auto end = route.target.find('}', index + 1U);
         if (end != std::string::npos) {
            const auto field_name = std::string_view{route.target}.substr(index + 1U, end - index - 1U);
            output += encode_query_component(require_tuple_argument_text(arguments, names, field_name));
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
                auto encoded = format_http_field(argument.value);
                if (!encoded.has_value()) {
                   FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                       "HTTP API query value cannot be encoded as text");
                }
                target.push_back(target.find('?') == std::string::npos ? '?' : '&');
                target += encode_query_component(route_query_name(route, names[Index]));
                target.push_back('=');
                target += encode_query_component(*encoded);
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
                   auto encoded = format_http_field(argument.value);
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
                   auto encoded = format_http_field(argument.value);
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
void reject_http_positional_parameters(const Tuple& arguments) {
   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
      (([&] {
          const auto& argument = std::get<Index>(arguments);
          using argument_type = std::remove_cvref_t<decltype(argument)>;
          if constexpr (detail::is_http_parameter_v<argument_type>) {
             static_cast<void>(argument);
             FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                 "HTTP positional methods cannot use fcl::http parameter wrappers");
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

template <typename Request>
std::optional<std::string> dto_body_bytes(Request& value) {
   auto result = std::optional<std::string>{};
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char*, auto member) {
         if (result.has_value()) {
            return;
         }
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_body_bytes_v<member_type>) {
            const auto& bytes = (value.*member).bytes;
            result.emplace();
            result->resize(bytes.size());
            if (!bytes.empty()) {
               std::memcpy(result->data(), bytes.data(), bytes.size());
            }
         }
      });
   }
   return result;
}

template <typename Request>
std::optional<std::string> dto_json_body(Request& value, bool allow_whole_request_body) {
   auto result = std::optional<std::string>{};
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char*, auto member) {
         if (result.has_value()) {
            return;
         }
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_body<member_type>::value) {
            const auto& body = value.*member;
            if (body.present) {
               auto encoded = fcl::json::write(body.value);
               if (!encoded.ok()) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                      "HTTP API request body cannot be encoded as JSON");
               }
               result = std::move(encoded.text);
            }
         }
      });
   }
   if constexpr (!detail::request_has_http_parameter_v<Request>) {
      if (!result.has_value() && allow_whole_request_body) {
         auto encoded = fcl::json::write(value);
         if (!encoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request cannot be encoded as JSON");
         }
         result = std::move(encoded.text);
      }
   }
   return result;
}

template <typename Request>
std::optional<std::string> dto_multipart_body(request& target, const api_route& route, const Request& value) {
   auto parts = std::vector<multipart_writer_part>{};

   auto append_field = [&](std::string_view name, std::string_view text) {
      parts.push_back(multipart_writer_part{
         .name = std::string{name},
         .body = std::string{text},
      });
   };

   auto append_file = [&](std::string_view name, const upload_part& part) {
      auto body = std::string{};
      if (!part.memory.empty()) {
         body.assign(reinterpret_cast<const char*>(part.memory.data()), part.memory.size());
      } else {
         body = part.text();
      }
      parts.push_back(multipart_writer_part{
         .name = std::string{name},
         .filename = part.filename,
         .content_type = part.content_type,
         .body = std::move(body),
      });
   };

   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         using member_type = std::remove_cvref_t<decltype(value.*member)>;
         if constexpr (detail::is_form<member_type>::value || detail::is_form_field<member_type>::value) {
            const auto& field = value.*member;
            if (field.present) {
               auto encoded = format_http_field(field.value);
               if (!encoded.has_value()) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                      "HTTP API form value cannot be encoded as text");
               }
               append_field(route_form_name(route, field_name), *encoded);
            }
         } else if constexpr (detail::is_upload_file_v<member_type>) {
            const auto& file = value.*member;
            if (file.present()) {
               append_file(route_form_name(route, field_name), file.part());
            }
         }
      });
   }

   if (parts.empty()) {
      return std::nullopt;
   }
   auto multipart = write_multipart_form(std::move(parts));
   target.set(field::content_type, multipart.content_type);
   return std::move(multipart.body);
}

template <typename Request>
std::optional<body_reader> bind_dto_request_body(request& target, const api_route& route, Request& value) {
   if (!uses_request_body(route.verb)) {
      return std::nullopt;
   }

   auto body_count = std::size_t{0};
   auto stream_body = take_body_stream(value, route);
   if (stream_body.has_value()) {
      ++body_count;
   }
   auto bytes_body = dto_body_bytes(value);
   if (bytes_body.has_value()) {
      ++body_count;
   }
   auto multipart_body = dto_multipart_body(target, route, value);
   if (multipart_body.has_value()) {
      ++body_count;
   }
   const auto allow_whole_request_body = route.verb != method::delete_;
   auto json_body = dto_json_body(value, allow_whole_request_body);
   if (json_body.has_value()) {
      ++body_count;
   }
   if (body_count > 1U) {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request has multiple body sources");
   }
   if (stream_body.has_value()) {
      return stream_body;
   }
   if (bytes_body.has_value()) {
      target.body() = std::move(*bytes_body);
      target.prepare_payload();
   } else if (multipart_body.has_value()) {
      target.body() = std::move(*multipart_body);
      target.prepare_payload();
   } else if (json_body.has_value()) {
      target.body() = std::move(*json_body);
      target.set(field::content_type, "application/json");
      target.prepare_payload();
   }
   return std::nullopt;
}

template <typename Tuple>
struct positional_client_request {
   request value;
   std::array<bool, std::tuple_size_v<Tuple>> consumed{};
};

template <auto Method, typename Request>
inline constexpr auto is_positional_http_method_v =
   fcl::api::method_argument_count_v<Method> != 1U ||
   !fcl::reflect::is_described_object_v<std::remove_cvref_t<Request>>;

template <typename Request>
request make_client_request(client& target, const api_route& route, Request& value) {
   auto request_value = request{};
   request_value.method(route.verb);
   auto rendered_target = render_route_target(route, value);
   append_route_query_fields(rendered_target, route, value);
   request_value.target(target.make_target(rendered_target));
   request_value.version(11);
   apply_route_headers(request_value, route, value);
   apply_route_cookies(request_value, value);
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
      auto body = bind_dto_request_body(request_value, route, value);

      auto response_value = body.has_value()
         ? co_await target.async_stream_request(std::move(request_value), std::move(*body))
         : co_await target.async_stream_request(std::move(request_value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return file_response::from_body(std::move(response_value.head), std::move(response_value.body));
      } else {
         co_return streaming_response::from_body(std::move(response_value.head), std::move(response_value.body));
      }
   } else if constexpr (detail::is_bytes_response_v<Response>) {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
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
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
         fcl::api::raise_remote_error(error, fcl::api::find_method(descriptor, route.method_name));
      }
      co_return Response{.status_code = response_value.result()};
   } else {
      auto request_value = make_client_request(target, route, value);
      auto body = bind_dto_request_body(request_value, route, value);
      auto response_value = body.has_value()
         ? co_await target.async_streaming_request(std::move(request_value), std::move(*body))
         : co_await target.async_request(std::move(request_value));
      if (response_value.result_int() < 200U || response_value.result_int() >= 300U) {
         auto error = decode_error_payload(response_value);
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
   reject_http_positional_parameters(value);
   auto request_parts = make_client_request(target, route, value, argument_names);
   auto request_body = bind_positional_request_body(request_parts.value, route, value, request_parts.consumed);
   if constexpr (detail::response_needs_stream_v<Response>) {
      auto response_value = request_body.has_value()
         ? co_await target.async_stream_request(std::move(request_parts.value), std::move(*request_body))
         : co_await target.async_stream_request(std::move(request_parts.value));
      if (response_value.head.result_int() < 200U || response_value.head.result_int() >= 300U) {
         response_value.head.body() = co_await read_bounded_error_body(response_value.body);
         auto error = decode_error_payload(response_value.head);
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
         auto error = decode_error_payload(response_value);
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
         if constexpr (!is_positional_http_method_v<Method, Request>) {
            if constexpr (detail::request_has_http_parameter_v<Request> ||
                          detail::request_needs_stream_v<Request> ||
                          detail::response_needs_stream_v<Response> ||
                          detail::is_bytes_response_v<Response> ||
                          detail::is_empty_response_v<Response>) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::protocol_error,
                                   "HTTP parameter methods require typed HTTP invocation");
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
         if constexpr (is_positional_http_method_v<Method, Request>) {
            output.emplace(co_await call_arguments<argument_tuple_t, Response>(
               target, descriptor, route, std::move(arguments), argument_names_for(descriptor, value.method)));
         } else {
            output.emplace(co_await call<Request, Response>(
               target, descriptor, route, std::move(std::get<0>(arguments))));
         }
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
   is_positional_http_method_v<Method, Request> ||
   (!detail::request_has_http_parameter_v<Request> &&
    !detail::request_needs_stream_v<Request> &&
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
