module;

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <forge/exceptions/macros.hpp>

export module forge.http.api.mapping;

import forge.api.connection;
import forge.api.descriptor;
import forge.api.types;
import forge.http.api.parameters;
import forge.http.exceptions;
import forge.http.types;
import forge.reflect.reflect;
import forge.schema.scalar;

export namespace forge::http::api {

enum class body_codec {
   json,
   xml,
};

enum class error_codec {
   json,
   xml,
};

struct field_binding {
   std::string field;
   std::string name;
};

struct route {
   method verb = method::get;
   std::string method_name;
   std::string target;
   status success_status = status::ok;
   std::vector<field_binding> headers;
   std::vector<field_binding> forms;
   std::optional<std::string> body_stream_field;
   bool response_file = false;
   bool response_stream = false;
   body_codec request_body_codec = body_codec::json;
   body_codec response_body_codec = body_codec::json;
   error_codec error_body_codec = error_codec::json;
};

class route_builder {
 public:
   route_builder(method verb, std::string method_name, std::string target, status success_status)
       : route_{.verb = verb,
                .method_name = std::move(method_name),
                .target = std::move(target),
                .success_status = success_status} {}

   route_builder&& header(std::string field, std::string name) && {
      route_.headers.push_back(field_binding{.field = std::move(field), .name = std::move(name)});
      return std::move(*this);
   }

   route_builder&& form(std::string field, std::string name) && {
      route_.forms.push_back(field_binding{.field = std::move(field), .name = std::move(name)});
      return std::move(*this);
   }

   route_builder&& body_stream(std::string field) && {
      route_.body_stream_field = std::move(field);
      return std::move(*this);
   }

   route_builder&& response_file() && {
      route_.response_file = true;
      return std::move(*this);
   }

   route_builder&& response_stream() && {
      route_.response_stream = true;
      return std::move(*this);
   }

   route_builder&& request_body_codec(body_codec value) && {
      route_.request_body_codec = value;
      return std::move(*this);
   }

   route_builder&& response_body_codec(body_codec value) && {
      route_.response_body_codec = value;
      return std::move(*this);
   }

   route_builder&& error_body_codec(error_codec value) && {
      route_.error_body_codec = value;
      return std::move(*this);
   }

   [[nodiscard]] route build() && {
      return std::move(route_);
   }

 private:
   route route_;
};

struct parsed_route {
   std::string path;
   std::vector<field_binding> query;
};

template <typename Interface> struct traits;

namespace detail {

using ::forge::http::detail::is_body;
using ::forge::http::detail::is_body_bytes_v;
using ::forge::http::detail::is_body_stream_v;
using ::forge::http::detail::is_bytes_response_v;
using ::forge::http::detail::is_cookie;
using ::forge::http::detail::is_empty_response_v;
using ::forge::http::detail::is_form;
using ::forge::http::detail::is_form_field;
using ::forge::http::detail::is_header;
using ::forge::http::detail::is_http_parameter_v;
using ::forge::http::detail::is_query;
using ::forge::http::detail::is_stream_response_v;
using ::forge::http::detail::is_streaming_response_v;
using ::forge::http::detail::is_upload_file_v;
using ::forge::http::detail::request_has_http_parameter_v;
using ::forge::http::detail::request_needs_stream_v;
using ::forge::http::detail::response_needs_stream_v;

[[nodiscard]] inline bool uses_request_body(method verb) noexcept {
   return verb == method::post || verb == method::put || verb == method::patch || verb == method::delete_;
}

[[nodiscard]] inline std::string trim_ascii(std::string_view value) {
   auto begin = std::size_t{0};
   while (begin != value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
      ++begin;
   }
   auto end = value.size();
   while (end != begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
      --end;
   }
   return std::string{value.substr(begin, end - begin)};
}

[[nodiscard]] inline std::string lower_ascii(std::string_view value) {
   auto output = std::string{};
   output.reserve(value.size());
   for (const auto character : value) {
      output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
   }
   return output;
}

[[nodiscard]] inline std::string media_type(std::string_view value) {
   const auto semicolon = value.find(';');
   return lower_ascii(trim_ascii(value.substr(0, semicolon)));
}

[[nodiscard]] inline bool has_structured_suffix(std::string_view type, std::string_view suffix) {
   return type.size() > suffix.size() && type.ends_with(suffix);
}

[[nodiscard]] inline bool media_type_matches(body_codec codec, std::string_view value) {
   const auto type = media_type(value);
   switch (codec) {
   case body_codec::json:
      return type == "application/json" || has_structured_suffix(type, "+json");
   case body_codec::xml:
      return type == "application/xml" || type == "text/xml" || has_structured_suffix(type, "+xml");
   }
   return false;
}

[[nodiscard]] inline std::string_view content_type(body_codec codec) noexcept {
   switch (codec) {
   case body_codec::json:
      return "application/json";
   case body_codec::xml:
      return "application/xml";
   }
   return "application/octet-stream";
}

[[nodiscard]] inline std::string_view content_type(error_codec codec) noexcept {
   switch (codec) {
   case error_codec::json:
      return "application/json";
   case error_codec::xml:
      return "application/xml";
   }
   return "application/octet-stream";
}

[[nodiscard]] inline bool q_value_allows(std::string_view parameter) {
   auto value = trim_ascii(parameter);
   if (value.empty()) {
      return true;
   }
   if (value.front() != '0') {
      return true;
   }
   const auto decimal = value.find('.');
   if (decimal == std::string::npos) {
      return false;
   }
   for (auto index = decimal + 1U; index != value.size(); ++index) {
      if (value[index] >= '1' && value[index] <= '9') {
         return true;
      }
   }
   return false;
}

[[nodiscard]] inline bool accept_item_allows(std::string_view item) {
   auto offset = std::size_t{0};
   while (offset <= item.size()) {
      const auto separator = item.find(';', offset);
      const auto end = separator == std::string_view::npos ? item.size() : separator;
      const auto parameter = trim_ascii(item.substr(offset, end - offset));
      if (offset != 0U) {
         const auto equals = parameter.find('=');
         if (equals != std::string::npos && lower_ascii(trim_ascii(parameter.substr(0, equals))) == "q") {
            return q_value_allows(parameter.substr(equals + 1U));
         }
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
   }
   return true;
}

[[nodiscard]] inline bool accept_media_matches(body_codec codec, std::string_view item) {
   const auto semicolon = item.find(';');
   const auto type = media_type(item.substr(0, semicolon));
   if (type == "*/*" || type == "*") {
      return true;
   }
   if (type == "application/*") {
      return codec == body_codec::json || codec == body_codec::xml;
   }
   if (type == "text/*") {
      return codec == body_codec::xml;
   }
   return media_type_matches(codec, type);
}

[[nodiscard]] inline bool accept_allows(body_codec codec, std::string_view value) {
   if (trim_ascii(value).empty()) {
      return true;
   }
   auto offset = std::size_t{0};
   while (offset <= value.size()) {
      const auto separator = value.find(',', offset);
      const auto end = separator == std::string_view::npos ? value.size() : separator;
      const auto item = value.substr(offset, end - offset);
      if (accept_item_allows(item) && accept_media_matches(codec, item)) {
         return true;
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
   }
   return false;
}

template <auto Method, typename Request>
inline constexpr auto is_positional_http_method_v =
   forge::api::method_argument_count_v<Method> != 1U ||
   !forge::reflect::is_described_object_v<std::remove_cvref_t<Request>>;

[[nodiscard]] inline bool unreserved(char value) noexcept {
   const auto ch = static_cast<unsigned char>(value);
   return std::isalnum(ch) != 0 || value == '-' || value == '.' || value == '_' || value == '~';
}

[[nodiscard]] inline std::string encode_uri_component(std::string_view value) {
   constexpr auto* hex = "0123456789ABCDEF";
   auto output = std::string{};
   output.reserve(value.size());
   for (const auto character : value) {
      if (unreserved(character)) {
         output.push_back(character);
         continue;
      }
      const auto byte = static_cast<unsigned char>(character);
      output.push_back('%');
      output.push_back(hex[(byte >> 4U) & 0x0FU]);
      output.push_back(hex[byte & 0x0FU]);
   }
   return output;
}

[[nodiscard]] inline std::string encode_path_segment(std::string_view value) {
   return encode_uri_component(value);
}

[[nodiscard]] inline std::string encode_query_component(std::string_view value) {
   return encode_uri_component(value);
}

template <typename T> [[nodiscard]] std::optional<std::string> format_http_field(const T& value) {
   using clean = std::remove_cvref_t<T>;
   if constexpr (detail::is_header<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else if constexpr (detail::is_query<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else if constexpr (detail::is_cookie<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else if constexpr (detail::is_body<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else if constexpr (detail::is_form<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else if constexpr (detail::is_form_field<clean>::value) {
      if (!value.present) {
         return std::nullopt;
      }
      return format_http_field(value.value);
   } else {
      return forge::schema::format_scalar_text(value);
   }
}

template <typename Request>
[[nodiscard]] std::optional<std::string> request_field_text(const Request& request, std::string_view name) {
   auto result = std::optional<std::string>{};
   if constexpr (forge::reflect::is_described_object_v<Request>) {
      forge::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         if (!result.has_value() && std::string_view{field_name} == name) {
            result = format_http_field(request.*member);
         }
      });
   }
   return result;
}

template <typename Request>
[[nodiscard]] std::string require_request_field(const Request& request, std::string_view name) {
   auto value = request_field_text(request, name);
   if (!value.has_value()) {
      FORGE_THROW_EXCEPTION(forge::http::exceptions::bad_request, "HTTP API route field is not available",
                          forge::exceptions::ctx("field", std::string{name}));
   }
   return *value;
}

[[nodiscard]] inline parsed_route parse_route_template(std::string_view target) {
   if (target.empty() || target.front() != '/') {
      FORGE_THROW_EXCEPTION(forge::http::exceptions::bad_request, "HTTP API route must start with /");
   }

   auto normalize_path = [](std::string_view path) {
      auto output = std::string{};
      output.reserve(path.size());
      for (auto offset = std::size_t{0}; offset < path.size();) {
         if (path[offset] == '/') {
            output.push_back('/');
            ++offset;
            continue;
         }

         const auto separator = path.find('/', offset);
         const auto end = separator == std::string_view::npos ? path.size() : separator;
         const auto segment = path.substr(offset, end - offset);
         if (segment.find('{') != std::string_view::npos || segment.find('}') != std::string_view::npos) {
            if (segment.size() <= 2U || segment.front() != '{' || segment.back() != '}') {
               FORGE_THROW_EXCEPTION(forge::http::exceptions::bad_request,
                                   "HTTP API path placeholders must occupy a complete segment");
            }
            const auto name = segment.substr(1U, segment.size() - 2U);
            for (const auto value : name) {
               if (std::isalnum(static_cast<unsigned char>(value)) == 0 && value != '_') {
                  FORGE_THROW_EXCEPTION(forge::http::exceptions::bad_request,
                                      "HTTP API path placeholder name is invalid");
               }
            }
            output.push_back(':');
            output += name;
         } else {
            output += segment;
         }
         offset = end;
      }
      return output;
   };

   const auto query_start = target.find('?');
   auto parsed = parsed_route{
       .path = normalize_path(target.substr(0, query_start)),
   };
   if (query_start == std::string_view::npos) {
      return parsed;
   }

   auto query = target.substr(query_start + 1U);
   auto offset = std::size_t{0};
   while (offset <= query.size()) {
      const auto separator = query.find('&', offset);
      const auto end = separator == std::string_view::npos ? query.size() : separator;
      const auto part = query.substr(offset, end - offset);
      const auto equals = part.find('=');
      if (equals != std::string_view::npos && equals + 2U <= part.size() && part[equals + 1U] == '{' &&
          part.back() == '}') {
         parsed.query.push_back(field_binding{
            .field = std::string{part.substr(equals + 2U, part.size() - equals - 3U)},
            .name = std::string{part.substr(0, equals)},
         });
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
   }
   return parsed;
}

template <typename Request> [[nodiscard]] std::string render_route_target(const route& route, const Request& request) {
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
         output += encode_path_segment(require_request_field(request, std::string_view{route.target}.substr(index + 1U,
                                                                                                            end - index - 1U)));
         index = end;
         continue;
      }
      if (current == '{') {
         const auto end = route.target.find('}', index + 1U);
         if (end != std::string::npos) {
            output += encode_query_component(require_request_field(request,
                                                                   std::string_view{route.target}.substr(index + 1U,
                                                                                                         end - index - 1U)));
            index = end + 1U;
            continue;
         }
      }
      output.push_back(current);
      ++index;
   }
   return output;
}

[[nodiscard]] inline std::vector<route> validate_routes(const forge::api::descriptor& descriptor,
                                                            std::vector<route> routes) {
   for (const auto& route : routes) {
      if (route.method_name.empty()) {
         FORGE_THROW_EXCEPTION(forge::api::exceptions::method_not_found, "HTTP API route method is empty");
      }
      if (forge::api::find_method(descriptor, route.method_name) == nullptr) {
         FORGE_THROW_EXCEPTION(forge::api::exceptions::method_not_found, "HTTP API route method is not in descriptor",
                             forge::exceptions::ctx("method", route.method_name));
      }
      static_cast<void>(parse_route_template(route.target));
   }
   return routes;
}

} // namespace detail

} // namespace forge::http::api
