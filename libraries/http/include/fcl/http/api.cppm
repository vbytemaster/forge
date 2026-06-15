module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.http.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.http.exceptions;
export import fcl.http.mapping;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.route_context;
import fcl.http.types;
import fcl.raw.raw;
import fcl.reflect.reflect;

export namespace fcl::http {

enum class body_codec {
   raw,
   json,
};

enum class api_error_profile {
   json,
};

struct api_route_options {
   std::vector<std::string> query;
   body_codec body = body_codec::raw;
   status success_status = status::ok;
   api_error_profile error_profile = api_error_profile::json;
};

class api_binding {
 public:
   using mount_step = std::function<void(router&, std::string_view)>;

   explicit api_binding(std::vector<mount_step> steps) : steps_{std::move(steps)} {}

   void mount(router& target, std::string_view base_path = {}) const {
      for (const auto& step : steps_) {
         step(target, base_path);
      }
   }

 private:
   std::vector<mount_step> steps_;
};

class api_builder {
 public:
   using mount_step = api_binding::mount_step;

   api_builder() = default;
   explicit api_builder(router& target) : target_{&target} {}

   api_builder& use(fcl::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& get(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::get, std::move(path), std::move(options)));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& post(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::post, std::move(path), std::move(options)));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& put(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::put, std::move(path), std::move(options)));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& del(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::delete_, std::move(path), std::move(options)));
      return *this;
   }

   template <auto Method, typename Request, typename Response> api_builder& route(api_route value) {
      const auto parsed = detail::parse_route_template(value.target);
      steps_.push_back(make_step<Method, Request, Response>(
         value.verb, parsed.path,
         api_route_options{
            .query = parsed.query,
            .success_status = value.success_status,
         }));
      return *this;
   }

   template <typename Interface> api_builder& bind() {
      return http_api_traits<Interface>::bind(*this);
   }

   api_builder& middleware(middleware_descriptor descriptor) {
      steps_.push_back([descriptor = std::move(descriptor)](router& target, std::string_view base_path) mutable {
         auto mounted = descriptor;
         mounted.path_prefix = join_path(base_path, mounted.path_prefix);
         target.use(std::move(mounted));
      });
      return *this;
   }

   [[nodiscard]] api_binding build() {
      static_cast<void>(target_);
      return api_binding{std::move(steps_)};
   }

 private:
   template <typename T> struct is_optional : std::false_type {};
   template <typename T> struct is_optional<std::optional<T>> : std::true_type {
      using value_type = T;
   };

   [[nodiscard]] static std::string normalize_base_path(std::string_view base_path) {
      if (base_path.empty() || base_path == "/") {
         return {};
      }
      if (base_path.front() != '/') {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API base path must start with /");
      }
      while (base_path.size() > 1U && base_path.back() == '/') {
         base_path.remove_suffix(1U);
      }
      return std::string{base_path};
   }

   [[nodiscard]] static std::string join_path(std::string_view base_path, std::string_view path) {
      if (path.empty() || path.front() != '/') {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API path must start with /");
      }
      auto base = normalize_base_path(base_path);
      if (base.empty()) {
         return std::string{path};
      }
      if (path == "/") {
         return base;
      }
      auto result = std::move(base);
      result += path;
      return result;
   }

   template <typename Interface, typename Request, typename Response>
   [[nodiscard]] static std::string method_name() {
      const auto descriptor = Interface::describe();
      auto result = std::optional<std::string>{};
      for (const auto& method_value : descriptor.methods) {
         if (method_value.request_type == typeid(Request) && method_value.response_type == typeid(Response)) {
            if (result.has_value()) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found,
                                   "HTTP API route method is ambiguous for request/response types");
            }
            result = method_value.name;
         }
      }
      if (!result.has_value()) {
         FCL_THROW_EXCEPTION(fcl::api::exceptions::method_not_found,
                             "HTTP API route method is not declared by descriptor");
      }
      return *result;
   }

   [[nodiscard]] static std::string json_escape(std::string_view value) {
      auto output = std::string{};
      output.reserve(value.size() + 8U);
      for (const auto character : value) {
         switch (character) {
         case '\\':
            output += "\\\\";
            break;
         case '"':
            output += "\\\"";
            break;
         case '\n':
            output += "\\n";
            break;
         case '\r':
            output += "\\r";
            break;
         case '\t':
            output += "\\t";
            break;
         default: {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 0x20U) {
               constexpr auto* hex = "0123456789abcdef";
               output += "\\u00";
               output.push_back(hex[(byte >> 4U) & 0x0FU]);
               output.push_back(hex[byte & 0x0FU]);
            } else {
               output.push_back(character);
            }
            break;
         }
         }
      }
      return output;
   }

   [[nodiscard]] static std::string render_error(const fcl::api::error_payload& error) {
      return std::string{"{\"error\":\""} + json_escape(error.error) + "\",\"message\":\"" +
             json_escape(error.message) +
             "\",\"retryable\":" + (error.retryable ? "true" : "false") + ",\"identity\":{\"category\":\"" +
             json_escape(error.identity.category) + "\",\"code\":" + std::to_string(error.identity.code) + "}}";
   }

   [[nodiscard]] static status http_status(fcl::api::status value) noexcept {
      return static_cast<status>(static_cast<unsigned>(value));
   }

   [[nodiscard]] static bool uses_request_body(method verb) noexcept {
      return verb == method::post || verb == method::put || verb == method::patch;
   }

   [[nodiscard]] static std::optional<std::string_view> route_value(const route_context& context,
                                                                    const api_route_options& options,
                                                                    std::string_view name) {
      if (const auto route = context.route_params.find(std::string{name}); route != context.route_params.end()) {
         return route->second;
      }

      const auto configured =
          std::find_if(options.query.begin(), options.query.end(),
                       [&](const std::string& query_name) { return std::string_view{query_name} == name; });
      if (configured == options.query.end()) {
         return std::nullopt;
      }

      for (const auto& query : context.parsed_target.query_params) {
         if (query.key == name) {
            return query.has_value ? std::string_view{query.value} : std::string_view{"true"};
         }
      }
      return std::nullopt;
   }

   template <typename T> static void assign_from_text(T& target, std::string_view value, std::string_view field) {
      using clean = std::remove_cvref_t<T>;
      if constexpr (is_optional<clean>::value) {
         typename is_optional<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target = std::move(parsed);
      } else if constexpr (std::is_same_v<clean, std::string>) {
         target = std::string{value};
      } else if constexpr (std::is_same_v<clean, bool>) {
         if (value == "true" || value == "1" || value == "yes" || value == "on") {
            target = true;
         } else if (value == "false" || value == "0" || value == "no" || value == "off") {
            target = false;
         } else {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API query boolean is invalid",
                                fcl::exceptions::ctx("field", std::string{field}));
         }
      } else if constexpr (std::is_integral_v<clean>) {
         if constexpr (std::is_signed_v<clean>) {
            auto parsed = 0LL;
            const auto* first = value.data();
            const auto* last = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(first, last, parsed);
            if (ec != std::errc{} || ptr != last || parsed < std::numeric_limits<clean>::min() ||
                parsed > std::numeric_limits<clean>::max()) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API query integer is invalid",
                                   fcl::exceptions::ctx("field", std::string{field}));
            }
            target = static_cast<clean>(parsed);
         } else {
            auto parsed = 0ULL;
            const auto* first = value.data();
            const auto* last = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(first, last, parsed);
            if (ec != std::errc{} || ptr != last || parsed > std::numeric_limits<clean>::max()) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API query integer is invalid",
                                   fcl::exceptions::ctx("field", std::string{field}));
            }
            target = static_cast<clean>(parsed);
         }
      } else if constexpr (std::is_floating_point_v<clean>) {
         auto copy = std::string{value};
         char* end = nullptr;
         errno = 0;
         const auto parsed = std::strtold(copy.c_str(), &end);
         if (errno != 0 || end != copy.c_str() + copy.size()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API query number is invalid",
                                fcl::exceptions::ctx("field", std::string{field}));
         }
         target = static_cast<clean>(parsed);
      } else {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API route field type is not supported",
                             fcl::exceptions::ctx("field", std::string{field}));
      }
   }

   template <typename Request>
   [[nodiscard]] static Request make_request_from_route(const route_context& context, const api_route_options& options) {
      auto request = Request{};
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            if (auto value = route_value(context, options, name); value.has_value()) {
               assign_from_text(request.*member, *value, name);
            }
         });
      }
      return request;
   }

   template <typename T>
   static void require_equal(const T& actual, const T& expected, std::string_view field) {
      if constexpr (requires { actual == expected; }) {
         if (!(actual == expected)) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API route field disagrees with body",
                                fcl::exceptions::ctx("field", std::string{field}));
         }
      } else {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API route field type is not comparable",
                             fcl::exceptions::ctx("field", std::string{field}));
      }
   }

   template <typename Request>
   static void validate_request_body_matches_route(const route_context& context, const api_route_options& options,
                                                   const fcl::api::bytes& payload) {
      auto request = Request{};
      try {
         request = fcl::api::unpack_body<Request>(payload);
      } catch (const std::exception&) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API request body is invalid");
      }

      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            if (auto value = route_value(context, options, name); value.has_value()) {
               auto expected = std::remove_cvref_t<decltype(request.*member)>{};
               assign_from_text(expected, *value, name);
               require_equal(request.*member, expected, name);
            }
         });
      }
   }

   template <auto Method, typename Request, typename Response>
   [[nodiscard]] mount_step make_step(method verb, std::string path, api_route_options options) {
      using interface_type = typename method_class<decltype(Method)>::type;
      auto plan = plan_;
      auto name = method_name<interface_type, Request, Response>();
      return [plan = std::move(plan), verb, path = std::move(path), options = std::move(options),
              name = std::move(name)](router& target, std::string_view base_path) {
         auto mounted_path = join_path(base_path, path);
         auto handler = [plan, options, name](route_context& context) -> boost::asio::awaitable<response> {
            if (plan.local == nullptr) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "HTTP API binding has no local registry");
            }
            auto payload = fcl::api::bytes{};
            if (uses_request_body(context.request.method())) {
               const auto& body = context.request.body();
               payload.assign(body.begin(), body.end());
               validate_request_body_matches_route<Request>(context, options, payload);
            } else {
               auto request = make_request_from_route<Request>(context, options);
               fcl::raw::pack(payload, request);
            }
            const auto api_descriptor = interface_type::describe();
            auto frame = fcl::api::frame{
                .kind = fcl::api::frame_kind::request,
                .id = {.value = 1},
                .api = {.id = api_descriptor.id,
                        .major = api_descriptor.version.major,
                        .min_revision = api_descriptor.version.revision},
                .method = name,
                .codec = {.value = options.body == body_codec::raw ? "fcl.raw" : "fcl.json"},
                .payload = std::move(payload),
            };
            auto response_frame = co_await plan.dispatch(std::move(frame));
            if (response_frame.kind == fcl::api::frame_kind::error) {
               const auto error = fcl::raw::unpack<fcl::api::error_payload>(response_frame.payload);
               co_return make_text_response(context.request, http_status(error.status_code), render_error(error),
                                            "application/json");
            }
            co_return make_text_response(context.request, options.success_status,
                                         std::string{response_frame.payload.begin(), response_frame.payload.end()},
                                         "application/octet-stream");
         };
         switch (verb) {
         case method::get:
            target.get(std::move(mounted_path), std::move(handler));
            break;
         case method::post:
            target.post(std::move(mounted_path), std::move(handler));
            break;
         case method::put:
            target.put(std::move(mounted_path), std::move(handler));
            break;
         case method::delete_:
            target.del(std::move(mounted_path), std::move(handler));
            break;
         default:
            FCL_THROW_EXCEPTION(fcl::http::exceptions::method_not_allowed, "unsupported HTTP API route verb");
         }
      };
   }

   template <typename> struct method_class;

   template <typename Class, typename Return, typename... Args> struct method_class<Return (Class::*)(Args...)> {
      using type = Class;
   };

   template <typename Class, typename Return, typename... Args> struct method_class<Return (Class::*)(Args...) const> {
      using type = Class;
   };

   router* target_ = nullptr;
   fcl::api::binding_plan plan_;
   std::vector<mount_step> steps_;
};

[[nodiscard]] inline api_builder api(router& target) {
   return api_builder{target};
}

[[nodiscard]] inline api_builder api() {
   return api_builder{};
}

} // namespace fcl::http
