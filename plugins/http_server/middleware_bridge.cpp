module;

#include <boost/asio/awaitable.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.plugins.http_server.plugin;

import fcl.http.middleware;
import fcl.http.route_context;
import fcl.http.types;
import fcl.plugins.http_server.middleware;

#include "details/middleware_bridge.hxx"

namespace fcl::plugins::http_server {
namespace {

constexpr std::string_view stream_token_header = "X-FCL-Stream-Token";

[[nodiscard]] fcl::http::middleware_phase to_http_phase(middleware_phase value) noexcept {
   switch (value) {
   case middleware_phase::request_context:
      return fcl::http::middleware_phase::request_context;
   case middleware_phase::security:
      return fcl::http::middleware_phase::security;
   case middleware_phase::limits:
      return fcl::http::middleware_phase::limits;
   case middleware_phase::before_handler:
      return fcl::http::middleware_phase::before_handler;
   case middleware_phase::after_handler:
      return fcl::http::middleware_phase::after_handler;
   case middleware_phase::error:
      return fcl::http::middleware_phase::error;
   }
   return fcl::http::middleware_phase::before_handler;
}

[[nodiscard]] std::string_view method_text(fcl::http::method value) noexcept {
   switch (value) {
   case fcl::http::method::delete_:
      return "DELETE";
   case fcl::http::method::get:
      return "GET";
   case fcl::http::method::head:
      return "HEAD";
   case fcl::http::method::options:
      return "OPTIONS";
   case fcl::http::method::patch:
      return "PATCH";
   case fcl::http::method::post:
      return "POST";
   case fcl::http::method::put:
      return "PUT";
   case fcl::http::method::unknown:
      return "UNKNOWN";
   }
   return "UNKNOWN";
}

[[nodiscard]] middleware_request make_request(const fcl::http::route_context& context) {
   auto headers = std::vector<header_entry>{};
   for (const auto& header : context.request.headers()) {
      headers.push_back(header_entry{.name = header.name, .value = header.text});
   }
   return middleware_request{
      .method = std::string{method_text(context.request.method())},
      .target = std::string{context.request.target()},
      .path = context.parsed_target.path,
      .headers = std::move(headers),
   };
}

[[nodiscard]] middleware_response make_response(fcl::http::response value) {
   auto result = middleware_response{};
   detail::middleware_bridge_access::set_status(result, value.result());
   detail::middleware_bridge_access::set_body(result, std::move(value.body()));
   for (const auto& header : value.headers()) {
      if (header_name_equal(header.name, fcl::http::field_name(fcl::http::field::content_type))) {
         detail::middleware_bridge_access::set_content_type(result, header.text);
         continue;
      }
      if (header_name_equal(header.name, fcl::http::field_name(fcl::http::field::content_length)) ||
          header_name_equal(header.name, fcl::http::field_name(fcl::http::field::transfer_encoding))) {
         continue;
      }
      if (header_name_equal(header.name, stream_token_header)) {
         detail::middleware_bridge_access::set_stream_token(result, header.text);
         continue;
      }
      detail::middleware_bridge_access::headers(result).push_back(
         header_entry{.name = header.name, .value = header.text});
   }
   return result;
}

[[nodiscard]] fcl::http::response make_http_response(const fcl::http::request& source, middleware_response value) {
   auto result = fcl::http::response{value.status(), source.version()};
   if (const auto& content_type = detail::middleware_bridge_access::content_type(value);
       content_type.has_value() && !content_type->empty()) {
      result.set(fcl::http::field::content_type, *content_type);
   }
   result.body() = detail::middleware_bridge_access::take_body(value);
   for (const auto& header : value.headers()) {
      if (header_name_equal(header.name, "Content-Length") || header_name_equal(header.name, "Transfer-Encoding")) {
         continue;
      }
      result.set(std::string_view{header.name}, std::string_view{header.value});
   }
   if (!result.body().empty()) {
      result.erase(stream_token_header);
   } else if (const auto& token = detail::middleware_bridge_access::stream_token(value); !token.empty()) {
      result.set(stream_token_header, token);
   }
   result.prepare_payload();
   result.keep_alive(source.keep_alive());
   return result;
}

} // namespace

fcl::http::middleware_descriptor to_http_middleware(middleware_descriptor descriptor) {
   return fcl::http::middleware_descriptor{
      .id = std::move(descriptor.id),
      .phase = to_http_phase(descriptor.phase),
      .order = descriptor.order,
      .path_prefix = std::move(descriptor.path_prefix),
      .handler =
         [descriptor = std::move(descriptor)](fcl::http::route_context& context,
                                              fcl::http::next_handler next)
            -> boost::asio::awaitable<fcl::http::response> {
            if (!descriptor.handler) {
               co_return co_await next();
            }
            auto request = make_request(context);
            auto response = co_await descriptor.handler(
               request,
               [next = std::move(next)]() mutable -> boost::asio::awaitable<middleware_response> {
                  auto raw = co_await next();
                  co_return make_response(std::move(raw));
               });
            co_return make_http_response(context.request, std::move(response));
         },
   };
}

} // namespace fcl::plugins::http_server
