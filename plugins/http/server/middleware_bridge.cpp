module;

#include <boost/asio/awaitable.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.http.server.plugin;

import forge.http.middleware;
import forge.http.route_context;
import forge.http.types;
import forge.plugins.http.server.middleware;

#include "details/middleware_bridge.hxx"

namespace forge::plugins::http::server {
namespace {

[[nodiscard]] forge::http::middleware_phase to_http_phase(middleware_phase value) noexcept {
   switch (value) {
   case middleware_phase::request_context:
      return forge::http::middleware_phase::request_context;
   case middleware_phase::security:
      return forge::http::middleware_phase::security;
   case middleware_phase::limits:
      return forge::http::middleware_phase::limits;
   case middleware_phase::before_handler:
      return forge::http::middleware_phase::before_handler;
   case middleware_phase::after_handler:
      return forge::http::middleware_phase::after_handler;
   case middleware_phase::error:
      return forge::http::middleware_phase::error;
   }
   return forge::http::middleware_phase::before_handler;
}

[[nodiscard]] std::string_view method_text(forge::http::method value) noexcept {
   switch (value) {
   case forge::http::method::delete_:
      return "DELETE";
   case forge::http::method::get:
      return "GET";
   case forge::http::method::head:
      return "HEAD";
   case forge::http::method::options:
      return "OPTIONS";
   case forge::http::method::patch:
      return "PATCH";
   case forge::http::method::post:
      return "POST";
   case forge::http::method::put:
      return "PUT";
   case forge::http::method::unknown:
      return "UNKNOWN";
   }
   return "UNKNOWN";
}

[[nodiscard]] middleware_request make_request(const forge::http::route_context& context) {
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

[[nodiscard]] middleware_response make_response(forge::http::response value) {
   auto result = middleware_response{};
   auto stream_state = forge::http::capture_stream_pass_through(value);
   detail::middleware_bridge_access::set_status(result, value.result());
   detail::middleware_bridge_access::set_body(result, std::move(value.body()));
   for (const auto& header : value.headers()) {
      if (forge::http::header_name_equal(header.name, forge::http::field_name(forge::http::field::content_type))) {
         detail::middleware_bridge_access::set_content_type(result, header.text);
         continue;
      }
      if (forge::http::header_name_equal(header.name, forge::http::field_name(forge::http::field::content_length)) ||
          forge::http::header_name_equal(header.name, forge::http::field_name(forge::http::field::transfer_encoding))) {
         continue;
      }
      detail::middleware_bridge_access::headers(result).push_back(
         header_entry{.name = header.name, .value = header.text});
   }
   detail::middleware_bridge_access::set_stream_state(result, std::move(stream_state));
   return result;
}

[[nodiscard]] forge::http::response make_http_response(const forge::http::request& source, middleware_response value) {
   auto result = forge::http::response{value.status(), source.version()};
   if (const auto& content_type = detail::middleware_bridge_access::content_type(value);
       content_type.has_value() && !content_type->empty()) {
      result.set(forge::http::field::content_type, *content_type);
   }
   result.body() = detail::middleware_bridge_access::take_body(value);
   for (const auto& header : value.headers()) {
      if (forge::http::header_name_equal(header.name, "Content-Length") ||
          forge::http::header_name_equal(header.name, "Transfer-Encoding")) {
         continue;
      }
      result.set(std::string_view{header.name}, std::string_view{header.value});
   }
   if (!result.body().empty()) {
      forge::http::clear_stream_pass_through(result);
   } else {
      forge::http::restore_stream_pass_through(result, detail::middleware_bridge_access::stream_state(value));
   }
   result.prepare_payload();
   result.keep_alive(source.keep_alive());
   return result;
}

} // namespace

forge::http::middleware_descriptor to_http_middleware(middleware_descriptor descriptor) {
   return forge::http::middleware_descriptor{
      .id = std::move(descriptor.id),
      .phase = to_http_phase(descriptor.phase),
      .order = descriptor.order,
      .path_prefix = std::move(descriptor.path_prefix),
      .handler =
         [descriptor = std::move(descriptor)](forge::http::route_context& context,
                                              forge::http::next_handler next)
            -> boost::asio::awaitable<forge::http::response> {
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

} // namespace forge::plugins::http::server
