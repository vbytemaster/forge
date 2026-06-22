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

export module fcl.http.api.proxy;

import fcl.api.connection;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.types;
export import fcl.api.handle;
export import fcl.http.api.client_response;
export import fcl.http.client;
import fcl.http.exceptions;
export import fcl.http.api.mapping;

export namespace fcl::http::api {

template <typename Interface> class proxy;

namespace detail {

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
route_call make_route_call(route route) {
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
   if constexpr (traits<Interface>::use_api_proxy) {
      co_return fcl::api::handle<Interface>{
         std::make_shared<fcl::api::proxy<Interface>>(
            traits<Interface>::make_invoker(value),
            Interface::ref())};
   } else {
      co_return fcl::api::handle<Interface>{std::make_shared<proxy<Interface>>(value)};
   }
}

} // namespace fcl::http::api
