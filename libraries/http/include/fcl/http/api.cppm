module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
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
import fcl.http.exceptions;
import fcl.http.body;
import fcl.http.binding;
import fcl.http.file;
export import fcl.http.mapping;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.route_context;
import fcl.http.stream;
import fcl.http.types;
import fcl.http.upload;
import fcl.json;
import fcl.reflect.reflect;
import fcl.schema.diagnostic;
import fcl.schema.object;

export namespace fcl::http {

enum class api_error_profile {
   json,
};

struct api_route_options {
   std::vector<api_field_binding> query;
   std::vector<api_field_binding> headers;
   std::vector<api_field_binding> forms;
   std::optional<std::string> body_stream_field;
   bool response_file = false;
   bool response_stream = false;
   status success_status = status::ok;
   api_error_profile error_profile = api_error_profile::json;
};

class api_binding {
 public:
   void mount(router& target, std::string_view base_path = {}) const {
      for (const auto& step : steps_) {
         step(target, base_path);
      }
   }

 private:
   friend class api_builder;

   using mount_action = std::function<void(router&, std::string_view)>;

   explicit api_binding(std::vector<mount_action> steps) : steps_{std::move(steps)} {}

   std::vector<mount_action> steps_;
};

class api_builder {
 public:
   api_builder() = default;
   explicit api_builder(router& target) : target_{&target} {}

   api_builder& use(fcl::api::binding_plan plan) {
      plan_ = std::move(plan);
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& get(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::get, std::move(path), std::move(options), {}));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& post(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::post, std::move(path), std::move(options), {}));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& put(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::put, std::move(path), std::move(options), {}));
      return *this;
   }

   template <auto Method, typename Request, typename Response>
   api_builder& del(std::string path, api_route_options options = {}) {
      steps_.push_back(make_step<Method, Request, Response>(method::delete_, std::move(path), std::move(options), {}));
      return *this;
   }

   template <auto Method, typename Request, typename Response> api_builder& route(api_route value) {
      const auto parsed = detail::parse_route_template(value.target);
      steps_.push_back(make_step<Method, Request, Response>(
         value.verb, parsed.path,
         api_route_options{
            .query = parsed.query,
            .headers = value.headers,
            .forms = value.forms,
            .body_stream_field = value.body_stream_field,
            .response_file = value.response_file,
            .response_stream = value.response_stream,
            .success_status = value.success_status,
         },
         value.method_name));
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
   using mount_action = api_binding::mount_action;

   template <typename T> struct is_optional : std::false_type {};
   template <typename T> struct is_optional<std::optional<T>> : std::true_type {
      using value_type = T;
   };

   template <typename Object> struct multipart_member_predicate {
      template <typename Descriptor>
      using fn = std::bool_constant<
         detail::is_form_field<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>::value ||
         detail::is_form<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>::value ||
         detail::is_upload_file_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
   };

   template <typename Object> struct body_bytes_member_predicate {
      template <typename Descriptor>
      using fn = std::bool_constant<
         detail::is_body_bytes_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
   };

   template <typename Object> struct body_stream_member_predicate {
      template <typename Descriptor>
      using fn = std::bool_constant<
         detail::is_body_stream_v<std::remove_cvref_t<decltype(std::declval<Object>().*Descriptor::pointer)>>>;
   };

   template <typename T, template <typename> typename Predicate, bool Described = fcl::reflect::is_described_object_v<T>>
   struct request_has_member : std::false_type {};

   template <typename T, template <typename> typename Predicate> struct request_has_member<T, Predicate, true> {
      using members = boost::describe::describe_members<std::remove_cvref_t<T>,
                                                        boost::describe::mod_any_access |
                                                           boost::describe::mod_inherited>;
      static constexpr auto value = boost::mp11::mp_any_of_q<members, Predicate<T>>::value;
   };

   template <typename Request>
   static constexpr auto request_has_multipart_v =
      request_has_member<Request, multipart_member_predicate>::value;

   template <typename Request>
   static constexpr auto request_has_body_bytes_v =
      request_has_member<Request, body_bytes_member_predicate>::value;

   template <typename Request>
   static constexpr auto request_has_body_stream_v =
      request_has_member<Request, body_stream_member_predicate>::value;

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

   [[nodiscard]] static bool is_json_content_type(std::string_view value) {
      if (value.empty()) {
         return true;
      }
      auto lower = std::string{};
      lower.reserve(value.size());
      for (const auto character : value) {
         lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
      }
      return lower == "application/json" || lower.starts_with("application/json;") ||
             lower.find("+json") != std::string::npos;
   }

   [[nodiscard]] static std::optional<std::string_view> route_value(const route_context& context,
                                                                    const api_route_options& options,
                                                                    std::string_view name) {
      if (const auto route = context.route_params.find(std::string{name}); route != context.route_params.end()) {
         return route->second;
      }

      const auto configured = std::find_if(options.query.begin(), options.query.end(),
                                           [&](const api_field_binding& binding) {
                                              return std::string_view{binding.field} == name;
                                           });
      if (configured == options.query.end()) {
         return std::nullopt;
      }

      for (const auto& query : context.parsed_target.query_params) {
         if (query.key == configured->name) {
            return query.has_value ? std::string_view{query.value} : std::string_view{"true"};
         }
      }
      return std::nullopt;
   }

   [[nodiscard]] static std::optional<std::string_view> query_value(const route_context& context,
                                                                    const api_route_options& options,
                                                                    std::string_view name) {
      auto wire_name = std::string{name};
      const auto configured = std::find_if(options.query.begin(), options.query.end(),
                                           [&](const api_field_binding& binding) {
                                              return std::string_view{binding.field} == name;
                                           });
      if (configured != options.query.end()) {
         wire_name = configured->name;
      }

      for (const auto& query : context.parsed_target.query_params) {
         if (query.key == wire_name) {
            return query.has_value ? std::string_view{query.value} : std::string_view{"true"};
         }
      }
      return std::nullopt;
   }

   template <typename T> static void assign_from_text(T& target, std::string_view value, std::string_view field) {
      using clean = std::remove_cvref_t<T>;
      if constexpr (detail::is_header<clean>::value) {
         typename detail::is_header<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (detail::is_query<clean>::value) {
         typename detail::is_query<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (detail::is_cookie<clean>::value) {
         typename detail::is_cookie<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (detail::is_body<clean>::value) {
         typename detail::is_body<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (detail::is_form<clean>::value) {
         typename detail::is_form<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (detail::is_form_field<clean>::value) {
         typename detail::is_form_field<clean>::value_type parsed{};
         assign_from_text(parsed, value, field);
         target.value = std::move(parsed);
         target.present = true;
      } else if constexpr (is_optional<clean>::value) {
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
   [[nodiscard]] static Request make_request_from_route(const route_context& context, const api_route_options& options,
                                                        bool body_decoded = false) {
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

   [[nodiscard]] static std::string header_name_from_field(std::string_view name) {
      auto output = std::string{};
      output.reserve(name.size());
      for (const auto character : name) {
         output.push_back(character == '_' ? '-' : character);
      }
      return output;
   }

   [[nodiscard]] static std::optional<std::string_view> header_value(const request& request_value,
                                                                      std::string_view name) {
      const auto lower_name = [&] {
         auto output = std::string{name};
         std::transform(output.begin(), output.end(), output.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
         });
         return output;
      }();
      for (const auto& field_value : request_value) {
         auto candidate = std::string{field_value.name_string()};
         std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
         });
         if (candidate == lower_name) {
            return std::string_view{field_value.value().data(), field_value.value().size()};
         }
      }
      return std::nullopt;
   }

   [[nodiscard]] static std::optional<std::string_view> cookie_value(const request& request_value,
                                                                     std::string_view name) {
      const auto header = header_value(request_value, "Cookie");
      if (!header.has_value()) {
         return std::nullopt;
      }

      auto text = *header;
      while (!text.empty()) {
         while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == ';')) {
            text.remove_prefix(1U);
         }
         const auto separator = text.find(';');
         const auto pair = text.substr(0, separator);
         const auto equals = pair.find('=');
         if (equals != std::string_view::npos) {
            auto key = pair.substr(0, equals);
            auto value = pair.substr(equals + 1U);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
               key.remove_suffix(1U);
            }
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
               value.remove_prefix(1U);
            }
            if (key == name) {
               return value;
            }
         }
         if (separator == std::string_view::npos) {
            break;
         }
         text.remove_prefix(separator + 1U);
      }
      return std::nullopt;
   }

   [[nodiscard]] static std::string mapped_name_or_default(const std::vector<api_field_binding>& bindings,
                                                           std::string_view field,
                                                           std::string (*default_name)(std::string_view)) {
      const auto found =
          std::find_if(bindings.begin(), bindings.end(),
                       [&](const api_field_binding& binding) { return std::string_view{binding.field} == field; });
      if (found != bindings.end()) {
         return found->name;
      }
      return default_name(field);
   }

   [[nodiscard]] static std::string identity_name(std::string_view value) {
      return std::string{value};
   }

   template <typename Request>
   static void bind_headers(Request& request, const route_context& context, const api_route_options& options) {
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            using member_type = std::remove_cvref_t<decltype(request.*member)>;
            if constexpr (detail::is_header<member_type>::value) {
               const auto header_name = mapped_name_or_default(options.headers, name, header_name_from_field);
               if (auto value = header_value(context.request, header_name); value.has_value()) {
                  assign_from_text(request.*member, *value, name);
               }
            }
         });
      }
   }

   template <typename Request> static void bind_body_bytes(Request& request, std::vector<std::byte> bytes) {
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char*, auto member) {
            using member_type = std::remove_cvref_t<decltype(request.*member)>;
            if constexpr (detail::is_body_bytes_v<member_type>) {
               request.*member = body_bytes{.bytes = bytes};
            }
         });
      }
   }

   template <typename Request>
   static void bind_body_stream(Request& request, body_reader reader, const api_route_options& options) {
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         auto bound = false;
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            using member_type = std::remove_cvref_t<decltype(request.*member)>;
            if constexpr (detail::is_body_stream_v<member_type>) {
               if (!options.body_stream_field.has_value() || *options.body_stream_field == name) {
                  request.*member = body_stream{reader};
                  bound = true;
               }
            }
         });
         if (options.body_stream_field.has_value() && !bound) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API body stream field is not described",
                                fcl::exceptions::ctx("field", *options.body_stream_field));
         }
      }
   }

   template <typename Request>
   static void bind_multipart(Request& request, const multipart_form& form, const api_route_options& options) {
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            using member_type = std::remove_cvref_t<decltype(request.*member)>;
            if constexpr (detail::is_form_field<member_type>::value) {
               const auto form_name = mapped_name_or_default(options.forms, name, identity_name);
               if (auto value = form.field(form_name); value.has_value()) {
                  assign_from_text(request.*member, *value, name);
               }
            } else if constexpr (detail::is_upload_file_v<member_type>) {
               const auto form_name = mapped_name_or_default(options.forms, name, identity_name);
               for (const auto& file : form.files) {
                  if (file.name == form_name) {
                     request.*member = upload_file{file};
                     break;
                  }
               }
            }
         });
      }
   }

   template <typename Request>
   static void validate_request_schema(const Request& request, std::string_view base_path = "http.request") {
      const auto rules = fcl::schema::rules<Request>::define();
      auto diagnostics = rules.validate(request, base_path);
      const auto error = std::find_if(diagnostics.begin(), diagnostics.end(), [](const fcl::schema::diagnostic& entry) {
         return entry.level == fcl::schema::severity::error;
      });
      if (error != diagnostics.end()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                             std::string{error->path} + ": " + error->code + ": " + error->message);
      }
   }

   template <typename Value>
   static void validate_bound_value_schema(const Value& value, std::string_view base_path) {
      if constexpr (fcl::reflect::is_described_object_v<Value>) {
         validate_request_schema(value, base_path);
      } else {
         static_cast<void>(value);
         static_cast<void>(base_path);
      }
   }

   template <typename T>
   static constexpr auto positional_argument_needs_stream_v =
      detail::is_body_stream_v<std::remove_cvref_t<T>> ||
      detail::is_body_bytes_v<std::remove_cvref_t<T>> ||
      detail::is_upload_file_v<std::remove_cvref_t<T>> ||
      detail::is_form_field<std::remove_cvref_t<T>>::value ||
      detail::is_form<std::remove_cvref_t<T>>::value;

   template <typename Tuple, std::size_t... Index>
   static consteval bool tuple_needs_stream(std::index_sequence<Index...>) {
      return (positional_argument_needs_stream_v<std::tuple_element_t<Index, Tuple>> || ...);
   }

   template <typename Tuple>
   static constexpr auto tuple_needs_stream_v =
      tuple_needs_stream<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});

   [[nodiscard]] static std::string argument_name(const fcl::api::method_descriptor& method_descriptor,
                                                  std::size_t index) {
      if (index >= method_descriptor.argument_names.size()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                             "HTTP API positional method argument name is missing",
                             fcl::exceptions::ctx("method", method_descriptor.name));
      }
      return method_descriptor.argument_names[index];
   }

   [[nodiscard]] static std::string diagnostic_message(const std::vector<fcl::schema::diagnostic>& diagnostics,
                                                       std::string_view fallback) {
      const auto error = std::find_if(diagnostics.begin(), diagnostics.end(), [](const fcl::schema::diagnostic& entry) {
         return entry.level == fcl::schema::severity::error;
      });
      if (error == diagnostics.end()) {
         return std::string{fallback};
      }
      return std::string{error->path} + ": " + error->code + ": " + error->message;
   }

   template <typename T>
   [[nodiscard]] static T decode_json_value(std::string_view body, std::string_view source_name) {
      auto decoded = fcl::json::read<T>(body,
                                        fcl::json::read_options{.source_name = std::string{source_name},
                                                                .unknown_fields =
                                                                   fcl::json::unknown_field_policy::error});
      if (!decoded.ok()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                             diagnostic_message(decoded.diagnostics, "HTTP API JSON request body is invalid"));
      }
      return std::move(decoded.value);
   }

   template <typename Argument>
   [[nodiscard]] static Argument make_positional_argument_from_http(const route_context& context,
                                                                   const api_route_options& options,
                                                                   std::string_view name) {
      using clean = std::remove_cvref_t<Argument>;
      auto result = clean{};
      if constexpr (detail::is_header<clean>::value) {
         const auto header_name = mapped_name_or_default(options.headers, name, header_name_from_field);
         if (auto value = header_value(context.request, header_name); value.has_value()) {
            assign_from_text(result, *value, name);
         }
      } else if constexpr (detail::is_query<clean>::value) {
         if (auto value = query_value(context, options, name); value.has_value()) {
            assign_from_text(result, *value, name);
         }
      } else if constexpr (detail::is_cookie<clean>::value) {
         if (auto value = cookie_value(context.request, name); value.has_value()) {
            assign_from_text(result, *value, name);
         }
      } else if constexpr (detail::is_body<clean>::value) {
         if (!context.request.body().empty()) {
            const auto content_type = context.request.find(field::content_type);
            if (content_type == context.request.end() || !is_json_content_type(std::string_view{content_type->value()})) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::unsupported_media_type,
                                   "HTTP API request body must be application/json");
            }
            using value_type = typename detail::is_body<clean>::value_type;
            result.value = decode_json_value<value_type>(context.request.body(), "http.request.body");
            validate_bound_value_schema(result.value, "http.request.body");
            result.present = true;
         }
      } else if constexpr (detail::is_form<clean>::value || detail::is_form_field<clean>::value ||
                           detail::is_upload_file_v<clean> || detail::is_body_bytes_v<clean> ||
                           detail::is_body_stream_v<clean>) {
         static_cast<void>(context);
         static_cast<void>(options);
      } else if (auto value = route_value(context, options, name); value.has_value()) {
         assign_from_text(result, *value, name);
      }
      return result;
   }

   template <typename Tuple, std::size_t... Index>
   [[nodiscard]] static Tuple make_positional_arguments_from_http(const route_context& context,
                                                                 const api_route_options& options,
                                                                 const fcl::api::method_descriptor& method_descriptor,
                                                                 std::index_sequence<Index...>) {
      return Tuple{make_positional_argument_from_http<std::tuple_element_t<Index, Tuple>>(
         context, options, argument_name(method_descriptor, Index))...};
   }

   template <typename Tuple>
   [[nodiscard]] static Tuple make_positional_arguments_from_http(const route_context& context,
                                                                 const api_route_options& options,
                                                                 const fcl::api::method_descriptor& method_descriptor) {
      return make_positional_arguments_from_http<Tuple>(
         context, options, method_descriptor, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   }

   template <typename Argument>
   static boost::asio::awaitable<Argument> make_positional_argument_from_stream(stream_request& stream,
                                                                               const api_route_options& options,
                                                                               const multipart_form* form,
                                                                               std::string_view name) {
      using clean = std::remove_cvref_t<Argument>;
      if constexpr (detail::is_body_stream_v<clean>) {
         co_return body_stream{std::move(stream.body)};
      } else if constexpr (detail::is_body_bytes_v<clean>) {
         auto text = co_await stream.body.async_read_all();
         auto bytes = std::vector<std::byte>(text.size());
         std::memcpy(bytes.data(), text.data(), text.size());
         co_return body_bytes{.bytes = std::move(bytes)};
      } else if constexpr (detail::is_form<clean>::value || detail::is_form_field<clean>::value) {
         auto result = clean{};
         if (form != nullptr) {
            const auto form_name = mapped_name_or_default(options.forms, name, identity_name);
            if (auto value = form->field(form_name); value.has_value()) {
               assign_from_text(result, *value, name);
            }
         }
         co_return result;
      } else if constexpr (detail::is_upload_file_v<clean>) {
         if (form != nullptr) {
            const auto form_name = mapped_name_or_default(options.forms, name, identity_name);
            for (const auto& file : form->files) {
               if (file.name == form_name) {
                  co_return upload_file{file};
               }
            }
         }
         co_return upload_file{};
      } else if constexpr (detail::is_body<clean>::value) {
         auto result = clean{};
         auto text = co_await stream.body.async_read_all();
         if (!text.empty()) {
            const auto content_type = stream.context.request.find(field::content_type);
            if (content_type == stream.context.request.end() ||
                !is_json_content_type(std::string_view{content_type->value()})) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::unsupported_media_type,
                                   "HTTP API request body must be application/json");
            }
            using value_type = typename detail::is_body<clean>::value_type;
            result.value = decode_json_value<value_type>(text, "http.request.body");
            validate_bound_value_schema(result.value, "http.request.body");
            result.present = true;
         }
         co_return result;
      } else {
         co_return make_positional_argument_from_http<Argument>(stream.context, options, name);
      }
   }

   template <typename Tuple, std::size_t... Index>
   static boost::asio::awaitable<Tuple> make_positional_arguments_from_stream_impl(
      stream_request& stream,
      const api_route_options& options,
      const fcl::api::method_descriptor& method_descriptor,
      const multipart_form* form,
      std::index_sequence<Index...>) {
      co_return Tuple{co_await make_positional_argument_from_stream<std::tuple_element_t<Index, Tuple>>(
         stream, options, form, argument_name(method_descriptor, Index))...};
   }

   template <typename Tuple>
   static boost::asio::awaitable<Tuple> make_positional_arguments_from_stream(
      stream_request& stream,
      const api_route_options& options,
      const fcl::api::method_descriptor& method_descriptor) {
      auto form = std::optional<multipart_form>{};
      if constexpr (tuple_needs_stream_v<Tuple>) {
         constexpr auto multipart_needed = []<std::size_t... Index>(std::index_sequence<Index...>) consteval {
            return ((detail::is_form<std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>>::value ||
                     detail::is_form_field<std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>>::value ||
                     detail::is_upload_file_v<std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>>) || ...);
         }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
         if constexpr (multipart_needed) {
            const auto content_type = stream.context.request.find(field::content_type);
            auto reader = upload_reader{std::move(stream.body)};
            form = co_await reader.async_read_multipart(
               content_type == stream.context.request.end() ? std::string_view{} : std::string_view{content_type->value()});
         }
      }
      co_return co_await make_positional_arguments_from_stream_impl<Tuple>(
         stream, options, method_descriptor, form.has_value() ? &*form : nullptr,
         std::make_index_sequence<std::tuple_size_v<Tuple>>{});
   }

   template <typename Request>
   [[nodiscard]] static Request decode_json_request_body(std::string_view body) {
      auto decoded = fcl::json::read<Request>(body,
                                              fcl::json::read_options{.source_name = "http.request",
                                                                      .unknown_fields =
                                                                         fcl::json::unknown_field_policy::error});
      if (!decoded.ok()) {
         FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                             diagnostic_message(decoded.diagnostics, "HTTP API JSON request body is invalid"));
      }
      return std::move(decoded.value);
   }

   template <typename Request>
   static void bind_route_query_headers(Request& request,
                                        const route_context& context,
                                        const api_route_options& options,
                                        bool body_was_decoded) {
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         fcl::reflect::for_each_member<Request>([&](const char* name, auto member) {
            using member_type = std::remove_cvref_t<decltype(request.*member)>;
            if constexpr (detail::is_header<member_type>::value || detail::is_form_field<member_type>::value ||
                          detail::is_body_stream_v<member_type> || detail::is_body_bytes_v<member_type> ||
                          detail::is_upload_file_v<member_type>) {
               return;
            }
            if (auto value = route_value(context, options, name); value.has_value()) {
               auto expected = std::remove_cvref_t<decltype(request.*member)>{};
               assign_from_text(expected, *value, name);
               if (body_was_decoded) {
                  require_equal(request.*member, expected, name);
               } else {
                  request.*member = std::move(expected);
               }
            }
         });
      }
      bind_headers(request, context, options);
   }

   template <typename Request>
   [[nodiscard]] static Request make_request_from_http(const route_context& context, const api_route_options& options) {
      return make_request_from_http<Request>(context, options, true);
   }

   template <typename Request>
   [[nodiscard]] static Request make_request_from_http(const route_context& context,
                                                       const api_route_options& options,
                                                       bool validate_schema) {
      auto request = Request{};
      const auto has_body = uses_request_body(context.request.method()) && !context.request.body().empty();
      if constexpr (!detail::request_needs_stream_v<Request>) {
         if (has_body) {
            const auto content_type = context.request.find(field::content_type);
            if (content_type == context.request.end() || !is_json_content_type(std::string_view{content_type->value()})) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::unsupported_media_type,
                                   "HTTP API request body must be application/json");
            }
            request = decode_json_request_body<Request>(context.request.body());
         }
      }
      bind_route_query_headers(request, context, options, has_body);
      if (validate_schema) {
         validate_request_schema(request);
      }
      return request;
   }

   template <typename Request>
   static boost::asio::awaitable<Request> make_request_from_stream(stream_request& stream,
                                                                   const api_route_options& options) {
      auto request = make_request_from_http<Request>(stream.context, options, false);
      if constexpr (fcl::reflect::is_described_object_v<Request>) {
         if constexpr (request_has_multipart_v<Request>) {
            const auto content_type = stream.context.request.find(field::content_type);
            auto reader = upload_reader{std::move(stream.body)};
            auto form = co_await reader.async_read_multipart(
               content_type == stream.context.request.end() ? std::string_view{} : std::string_view{content_type->value()});
            bind_multipart(request, form, options);
         } else if constexpr (request_has_body_bytes_v<Request>) {
            auto text = co_await stream.body.async_read_all();
            auto bytes = std::vector<std::byte>(text.size());
            std::memcpy(bytes.data(), text.data(), text.size());
            bind_body_bytes(request, std::move(bytes));
         } else if constexpr (request_has_body_stream_v<Request>) {
            bind_body_stream(request, std::move(stream.body), options);
         } else if (uses_request_body(stream.context.request.method())) {
            auto text = co_await stream.body.async_read_all();
            if (!text.empty()) {
               const auto content_type = stream.context.request.find(field::content_type);
               if (content_type == stream.context.request.end() ||
                   !is_json_content_type(std::string_view{content_type->value()})) {
                  FCL_THROW_EXCEPTION(fcl::http::exceptions::unsupported_media_type,
                                      "HTTP API request body must be application/json");
               }
               request = decode_json_request_body<Request>(text);
               bind_route_query_headers(request, stream.context, options, true);
            }
         }
      }
      validate_request_schema(request);
      co_return request;
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

   [[nodiscard]] static response make_validation_response(const request& request_value, std::string_view message) {
      return make_text_response(
         request_value, static_cast<status>(422),
         std::string{"{\"error\":\"validation_error\",\"message\":\""} + json_escape(message) + "\"}",
         "application/json");
   }

   template <typename Response> [[nodiscard]] static response make_success_response(const request& request_value,
                                                                                    status success_status,
                                                                                    const Response& value) {
      if constexpr (detail::is_bytes_response_v<Response>) {
         auto body = std::string{reinterpret_cast<const char*>(value.bytes.data()), value.bytes.size()};
         return make_text_response(request_value, value.status_code, std::move(body), value.content_type);
      } else if constexpr (detail::is_empty_response_v<Response>) {
         auto output = response{value.status_code, request_value.version()};
         output.prepare_payload();
         output.keep_alive(request_value.keep_alive());
         return output;
      } else {
         auto encoded = fcl::json::write(value);
         if (!encoded.ok()) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request, "HTTP API response cannot be encoded as JSON");
         }
         return make_text_response(request_value, success_status, std::move(encoded.text), "application/json");
      }
   }

   template <typename Response>
   static boost::asio::awaitable<stream_response> make_success_stream_response(const request& request_value,
                                                                               status success_status,
                                                                               Response value) {
      if constexpr (std::is_same_v<std::remove_cvref_t<Response>, file_response>) {
         co_return co_await std::move(value).materialize(request_value);
      } else if constexpr (detail::is_streaming_response_v<Response>) {
         co_return std::move(value).materialize(request_value, success_status);
      } else if constexpr (detail::is_stream_response_v<Response>) {
         co_return std::move(value);
      } else {
         co_return stream_response::buffered(make_success_response(request_value, success_status, value));
      }
   }

   template <auto Method, typename Interface, typename Request, typename Response>
   static boost::asio::awaitable<Response> invoke_local(const fcl::api::binding_plan& plan, Request request) {
      auto implementation = plan.local->get<Interface>(Interface::ref());
      co_return co_await std::invoke(Method, *implementation.shared(), std::move(request));
   }

   template <auto Method, typename Interface, typename Tuple, typename Response>
   static boost::asio::awaitable<Response> invoke_local_arguments(const fcl::api::binding_plan& plan, Tuple arguments) {
      auto implementation = plan.local->get<Interface>(Interface::ref());
      co_return co_await std::apply(
         [&](auto&&... args) {
            return std::invoke(Method, *implementation.shared(), std::forward<decltype(args)>(args)...);
         },
         std::move(arguments));
   }

   static stream_response buffered(response value) {
      return stream_response::buffered(std::move(value));
   }

   template <typename Response> static void validate_response_file_option(const api_route_options& options) {
      constexpr auto response_is_file = std::is_same_v<std::remove_cvref_t<Response>, file_response>;
      constexpr auto response_is_stream = detail::is_streaming_response_v<Response>;
      if constexpr (response_is_file) {
         if (!options.response_file) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                "HTTP API file response route requires FCL_HTTP_RESPONSE_FILE");
         }
      } else {
         if (options.response_file) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                "FCL_HTTP_RESPONSE_FILE requires fcl::http::file_response");
         }
      }
      if constexpr (response_is_stream) {
         if (!options.response_stream) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                "HTTP API streaming response route requires FCL_HTTP_RESPONSE_STREAM");
         }
      } else {
         if (options.response_stream) {
            FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                "FCL_HTTP_RESPONSE_STREAM requires fcl::http::streaming_response");
         }
      }
   }

   template <auto Method, typename Request, typename Response>
   [[nodiscard]] mount_action make_step(method verb, std::string path, api_route_options options,
                                        std::string explicit_name) {
      using interface_type = typename method_class<decltype(Method)>::type;
      using argument_tuple = fcl::api::method_argument_tuple_t<Method>;
      auto plan = plan_;
      auto name = explicit_name.empty() ? method_name<interface_type, Request, Response>() : std::move(explicit_name);
      return [plan = std::move(plan), verb, path = std::move(path), options = std::move(options),
              name = std::move(name)](router& target, std::string_view base_path) {
         validate_response_file_option<Response>(options);
         auto mounted_path = join_path(base_path, path);
         if constexpr (detail::request_needs_stream_v<Request> || tuple_needs_stream_v<argument_tuple> ||
                       detail::response_needs_stream_v<Response>) {
            auto stream_handler = [plan, options, name](stream_request& request_value)
               -> boost::asio::awaitable<stream_response> {
               if (plan.local == nullptr) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version,
                                      "HTTP API binding has no local registry");
               }
               const auto api_descriptor = interface_type::describe();
               const auto* method_descriptor = fcl::api::find_method(api_descriptor, name);
               try {
                  if constexpr (fcl::api::method_argument_count_v<Method> != 1U) {
                     if (method_descriptor == nullptr || method_descriptor->argument_names.empty()) {
                        FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                            "HTTP API positional method is missing argument metadata");
                     }
                     auto arguments =
                        co_await make_positional_arguments_from_stream<argument_tuple>(request_value, options,
                                                                                       *method_descriptor);
                     auto value = co_await invoke_local_arguments<Method, interface_type, argument_tuple, Response>(
                        plan, std::move(arguments));
                     co_return co_await make_success_stream_response(request_value.context.request, options.success_status,
                                                                     std::move(value));
                  } else {
                     if (method_descriptor != nullptr && !method_descriptor->argument_names.empty()) {
                        auto arguments =
                           co_await make_positional_arguments_from_stream<argument_tuple>(request_value, options,
                                                                                          *method_descriptor);
                        auto value = co_await invoke_local_arguments<Method, interface_type, argument_tuple, Response>(
                           plan, std::move(arguments));
                        co_return co_await make_success_stream_response(request_value.context.request,
                                                                        options.success_status, std::move(value));
                     } else {
                        auto request = co_await make_request_from_stream<Request>(request_value, options);
                        auto value =
                           co_await invoke_local<Method, interface_type, Request, Response>(plan, std::move(request));
                        co_return co_await make_success_stream_response(request_value.context.request,
                                                                        options.success_status, std::move(value));
                     }
                  }
               } catch (const fcl::http::exceptions::unsupported_media_type& error) {
                  co_return buffered(make_text_response(
                     request_value.context.request, status::unsupported_media_type,
                     std::string{"{\"error\":\"unsupported_media_type\",\"message\":\""} + json_escape(error.message()) +
                        "\"}",
                     "application/json"));
               } catch (const fcl::http::exceptions::bad_request& error) {
                  co_return buffered(make_validation_response(request_value.context.request, error.message()));
               } catch (const fcl::exceptions::base& error) {
                  if (method_descriptor != nullptr) {
                     const auto payload = fcl::api::project_error(*method_descriptor, error);
                     co_return buffered(make_text_response(request_value.context.request, http_status(payload.status_code),
                                                           render_error(payload), "application/json"));
                  }
                  const auto payload = fcl::api::make_internal_error_payload();
                  co_return buffered(make_text_response(request_value.context.request, http_status(payload.status_code),
                                                        render_error(payload), "application/json"));
               }
            };
            switch (verb) {
            case method::get:
               target.get_stream(std::move(mounted_path), std::move(stream_handler));
               break;
            case method::head:
               target.head_stream(std::move(mounted_path), std::move(stream_handler));
               break;
            case method::post:
               target.post_stream(std::move(mounted_path), std::move(stream_handler));
               break;
            case method::put:
               target.put_stream(std::move(mounted_path), std::move(stream_handler));
               break;
            case method::patch:
               target.patch_stream(std::move(mounted_path), std::move(stream_handler));
               break;
            default:
               FCL_THROW_EXCEPTION(fcl::http::exceptions::method_not_allowed, "unsupported HTTP API stream route verb");
            }
            } else {
               auto handler = [plan, options, name](route_context& context) -> boost::asio::awaitable<response> {
               if (plan.local == nullptr) {
                  FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version,
                                      "HTTP API binding has no local registry");
               }
               const auto api_descriptor = interface_type::describe();
               const auto* method_descriptor = fcl::api::find_method(api_descriptor, name);
               try {
                  if constexpr (fcl::api::method_argument_count_v<Method> != 1U) {
                     if (method_descriptor == nullptr || method_descriptor->argument_names.empty()) {
                        FCL_THROW_EXCEPTION(fcl::http::exceptions::bad_request,
                                            "HTTP API positional method is missing argument metadata");
                     }
                     auto arguments = make_positional_arguments_from_http<argument_tuple>(context, options,
                                                                                         *method_descriptor);
                     auto value = co_await invoke_local_arguments<Method, interface_type, argument_tuple, Response>(
                        plan, std::move(arguments));
                     co_return make_success_response(context.request, options.success_status, value);
                  } else {
                     if (method_descriptor != nullptr && !method_descriptor->argument_names.empty()) {
                        auto arguments = make_positional_arguments_from_http<argument_tuple>(context, options,
                                                                                            *method_descriptor);
                        auto value = co_await invoke_local_arguments<Method, interface_type, argument_tuple, Response>(
                           plan, std::move(arguments));
                        co_return make_success_response(context.request, options.success_status, value);
                     } else {
                        auto request = make_request_from_http<Request>(context, options);
                        auto value =
                           co_await invoke_local<Method, interface_type, Request, Response>(plan, std::move(request));
                        co_return make_success_response(context.request, options.success_status, value);
                     }
                  }
               } catch (const fcl::http::exceptions::unsupported_media_type& error) {
                  co_return make_text_response(context.request, status::unsupported_media_type,
                                               std::string{"{\"error\":\"unsupported_media_type\",\"message\":\""} +
                                                  json_escape(error.message()) + "\"}",
                                               "application/json");
               } catch (const fcl::http::exceptions::bad_request& error) {
                  co_return make_validation_response(context.request, error.message());
               } catch (const fcl::exceptions::base& error) {
                  if (method_descriptor != nullptr) {
                     const auto payload = fcl::api::project_error(*method_descriptor, error);
                     co_return make_text_response(context.request, http_status(payload.status_code), render_error(payload),
                                                  "application/json");
                  }
                  const auto payload = fcl::api::make_internal_error_payload();
                  co_return make_text_response(context.request, http_status(payload.status_code), render_error(payload),
                                               "application/json");
               }
            };
         switch (verb) {
         case method::get:
            target.get(std::move(mounted_path), std::move(handler));
            break;
         case method::head:
            target.head(std::move(mounted_path), std::move(handler));
            break;
         case method::post:
            target.post(std::move(mounted_path), std::move(handler));
            break;
         case method::put:
            target.put(std::move(mounted_path), std::move(handler));
            break;
         case method::patch:
            target.patch(std::move(mounted_path), std::move(handler));
            break;
         case method::delete_:
            target.del(std::move(mounted_path), std::move(handler));
            break;
         default:
            FCL_THROW_EXCEPTION(fcl::http::exceptions::method_not_allowed, "unsupported HTTP API route verb");
         }
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
   std::vector<mount_action> steps_;
};

[[nodiscard]] inline api_builder api(router& target) {
   return api_builder{target};
}

[[nodiscard]] inline api_builder api() {
   return api_builder{};
}

} // namespace fcl::http
