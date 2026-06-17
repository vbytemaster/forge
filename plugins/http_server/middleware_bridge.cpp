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

[[nodiscard]] middleware_request make_request(const fcl::http::route_context& context) {
   auto headers = std::vector<header_entry>{};
   for (const auto& header : context.request) {
      headers.push_back(header_entry{.name = std::string{header.name_string()},
                                     .value = std::string{header.value()}});
   }
   return middleware_request{
      .method = std::string{context.request.method_string()},
      .target = std::string{context.request.target()},
      .path = context.parsed_target.path,
      .headers = std::move(headers),
   };
}

[[nodiscard]] middleware_response make_response(fcl::http::response value) {
   auto headers = std::vector<header_entry>{};
   auto content_type = std::string{};
   for (const auto& header : value) {
      if (header.name() == fcl::http::field::content_type) {
         content_type = std::string{header.value()};
         continue;
      }
      if (header.name() == fcl::http::field::content_length ||
          header.name() == fcl::http::field::transfer_encoding) {
         continue;
      }
      headers.push_back(header_entry{.name = std::string{header.name_string()},
                                     .value = std::string{header.value()}});
   }
   return middleware_response{
      .status = value.result(),
      .headers = std::move(headers),
      .body = std::move(value.body()),
      .content_type = std::move(content_type),
   };
}

[[nodiscard]] fcl::http::response make_http_response(const fcl::http::request& source, middleware_response value) {
   auto result = fcl::http::make_text_response(source, value.status, std::move(value.body),
                                               value.content_type.empty() ? "text/plain" : value.content_type);
   for (auto& header : value.headers) {
      if (header_name_equal(header.name, "Content-Length") || header_name_equal(header.name, "Transfer-Encoding")) {
         continue;
      }
      result.set(std::string_view{header.name}, std::string_view{header.value});
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
