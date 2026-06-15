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

#include <fcl/exceptions/macros.hpp>

export module fcl.http.mapping;

import fcl.api.connection;
import fcl.api.descriptor;
import fcl.http.exceptions;
import fcl.http.types;
import fcl.reflect.reflect;

export namespace fcl::http {

struct api_field_binding {
   std::string field;
   std::string name;
};

struct api_route {
   method verb = method::get;
   std::string method_name;
   std::string target;
   status success_status = status::ok;
   std::vector<api_field_binding> headers;
   std::vector<api_field_binding> forms;
   std::optional<std::string> body_stream_field;
   bool response_file = false;
};

class api_route_builder {
 public:
   api_route_builder(method verb, std::string method_name, std::string target, status success_status)
       : route_{.verb = verb,
                .method_name = std::move(method_name),
                .target = std::move(target),
                .success_status = success_status} {}

   api_route_builder&& header(std::string field, std::string name) && {
      route_.headers.push_back(api_field_binding{.field = std::move(field), .name = std::move(name)});
      return std::move(*this);
   }

   api_route_builder&& form(std::string field, std::string name) && {
      route_.forms.push_back(api_field_binding{.field = std::move(field), .name = std::move(name)});
      return std::move(*this);
   }

   api_route_builder&& body_stream(std::string field) && {
      route_.body_stream_field = std::move(field);
      return std::move(*this);
   }

   api_route_builder&& response_file() && {
      route_.response_file = true;
      return std::move(*this);
   }

   [[nodiscard]] api_route build() && {
      return std::move(route_);
   }

 private:
   api_route route_;
};

struct parsed_api_route {
   std::string path;
   std::vector<std::string> query;
};

template <typename Interface> struct http_api_traits;

namespace detail {

[[nodiscard]] inline bool uses_request_body(method verb) noexcept {
   return verb == method::post || verb == method::put || verb == method::patch;
}

[[nodiscard]] inline bool unreserved(char value) noexcept {
   const auto ch = static_cast<unsigned char>(value);
   return std::isalnum(ch) != 0 || value == '-' || value == '.' || value == '_' || value == '~';
}

[[nodiscard]] inline std::string percent_encode(std::string_view value) {
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

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {
   using value_type = T;
};

template <typename T> [[nodiscard]] std::optional<std::string> field_to_text(const T& value) {
   using clean = std::remove_cvref_t<T>;
   if constexpr (is_optional<clean>::value) {
      if (!value.has_value()) {
         return std::nullopt;
      }
      return field_to_text(*value);
   } else if constexpr (std::is_same_v<clean, std::string>) {
      return value;
   } else if constexpr (std::is_same_v<clean, bool>) {
      return value ? std::string{"true"} : std::string{"false"};
   } else if constexpr (std::is_integral_v<clean>) {
      return std::to_string(value);
   } else if constexpr (std::is_floating_point_v<clean>) {
      auto stream = std::ostringstream{};
      stream << value;
      return stream.str();
   } else {
      return std::nullopt;
   }
}

template <typename Request>
[[nodiscard]] std::optional<std::string> request_field_text(const Request& request, std::string_view name) {
   auto result = std::optional<std::string>{};
   if constexpr (fcl::reflect::is_described_object_v<Request>) {
      fcl::reflect::for_each_member<Request>([&](const char* field_name, auto member) {
         if (!result.has_value() && std::string_view{field_name} == name) {
            result = field_to_text(request.*member);
         }
      });
   }
   return result;
}

template <typename Request>
[[nodiscard]] std::string require_request_field(const Request& request, std::string_view name) {
   auto value = request_field_text(request, name);
   if (!value.has_value()) {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API route field is not available",
                          fcl::exceptions::ctx("field", std::string{name}));
   }
   return *value;
}

[[nodiscard]] inline parsed_api_route parse_route_template(std::string_view target) {
   if (target.empty() || target.front() != '/') {
      FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API route must start with /");
   }

   const auto query_start = target.find('?');
   auto parsed = parsed_api_route{
       .path = std::string{target.substr(0, query_start)},
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
         parsed.query.emplace_back(part.substr(equals + 2U, part.size() - equals - 3U));
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
   }
   return parsed;
}

template <typename Request> [[nodiscard]] std::string render_route_target(const api_route& route, const Request& request) {
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
         output += percent_encode(require_request_field(request, std::string_view{route.target}.substr(index + 1U,
                                                                                                       end - index - 1U)));
         index = end;
         continue;
      }
      if (current == '{') {
         const auto end = route.target.find('}', index + 1U);
         if (end != std::string::npos) {
            output += percent_encode(require_request_field(request, std::string_view{route.target}.substr(index + 1U,
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

[[nodiscard]] inline std::vector<api_route> validate_routes(const fcl::api::descriptor& descriptor,
                                                            std::vector<api_route> routes) {
   for (const auto& route : routes) {
      if (route.method_name.empty()) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found, "HTTP API route method is empty");
      }
      if (fcl::api::find_method(descriptor, route.method_name) == nullptr) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found, "HTTP API route method is not in descriptor",
                             fcl::exceptions::ctx("method", route.method_name));
      }
      static_cast<void>(parse_route_template(route.target));
   }
   return routes;
}

} // namespace detail

} // namespace fcl::http
