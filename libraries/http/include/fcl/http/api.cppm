module;

#include <boost/asio/awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

export module fcl.http.api;

import fcl.api;
import fcl.asio.blocking;
import fcl.http.exceptions;
import fcl.http.middleware;
import fcl.http.router;
import fcl.http.route_context;
import fcl.http.types;
import fcl.raw.raw;

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
   using mount_step = std::function<void(router&)>;

   explicit api_binding(std::vector<mount_step> steps) : steps_{std::move(steps)} {}

   void mount(router& target) const {
      for (const auto& step : steps_) {
         step(target);
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

   api_builder& middleware(middleware_descriptor descriptor) {
      steps_.push_back([descriptor = std::move(descriptor)](router& target) mutable { target.use(std::move(descriptor)); });
      return *this;
   }

   [[nodiscard]] api_binding build() {
      static_cast<void>(target_);
      return api_binding{std::move(steps_)};
   }

 private:
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

   [[nodiscard]] static std::string render_error(const fcl::api::error_payload& error) {
      return std::string{"{\"error\":\""} + error.error + "\",\"message\":\"" + error.message +
             "\",\"retryable\":" + (error.retryable ? "true" : "false") + ",\"identity\":{\"category\":\"" +
             error.identity.category + "\",\"code\":" + std::to_string(error.identity.code) + "}}";
   }

   [[nodiscard]] static status http_status(fcl::api::status value) noexcept {
      return static_cast<status>(static_cast<unsigned>(value));
   }

   [[nodiscard]] static bool uses_request_body(method verb) noexcept {
      return verb == method::post || verb == method::put || verb == method::patch;
   }

   template <auto Method, typename Request, typename Response>
   [[nodiscard]] mount_step make_step(method verb, std::string path, api_route_options options) {
      using interface_type = typename method_class<decltype(Method)>::type;
      auto plan = plan_;
      auto name = method_name<interface_type, Request, Response>();
      return [plan = std::move(plan), verb, path = std::move(path), options = std::move(options),
              name = std::move(name)](router& target) {
         auto handler = [plan, options, name](route_context& context) -> response {
            if (plan.local == nullptr) {
               FCL_THROW_EXCEPTION(fcl::api::exceptions::incompatible_version, "HTTP API binding has no local registry");
            }
            if (context.runtime == nullptr) {
               FCL_THROW_EXCEPTION(fcl::http::exceptions::internal, "HTTP API route has no runtime boundary");
            }
            auto request = Request{};
            auto payload = fcl::api::bytes{};
            if (uses_request_body(context.request.method())) {
               const auto& body = context.request.body();
               payload.assign(body.begin(), body.end());
            } else {
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
            auto response_frame = fcl::asio::blocking::run(*context.runtime, plan.dispatch(std::move(frame)));
            if (response_frame.kind == fcl::api::frame_kind::error) {
               const auto error = fcl::raw::unpack<fcl::api::error_payload>(response_frame.payload);
               return make_text_response(context.request, http_status(error.status_code), render_error(error),
                                         "application/json");
            }
            return make_text_response(context.request, options.success_status,
                                      std::string{response_frame.payload.begin(), response_frame.payload.end()},
                                      "application/octet-stream");
         };
         switch (verb) {
         case method::get:
            target.get(path, std::move(handler));
            break;
         case method::post:
            target.post(path, std::move(handler));
            break;
         case method::put:
            target.put(path, std::move(handler));
            break;
         case method::delete_:
            target.del(path, std::move(handler));
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
