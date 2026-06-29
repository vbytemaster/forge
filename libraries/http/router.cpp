module;

#include <cstdint>
#include <algorithm>
#include <coroutine>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.http.router;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.http.exceptions;
import forge.http.middleware;
import forge.http.stream;
import forge.http.target;
import forge.json;

namespace forge::http {
namespace {

std::string http_error_name(int code) {
   switch (code) {
   case 400:
      return "bad_request";
   case 401:
      return "unauthorized";
   case 403:
      return "forbidden";
   case 404:
      return "not_found";
   case 405:
      return "method_not_allowed";
   case 406:
      return "not_acceptable";
   case 409:
      return "conflict";
   case 413:
      return "payload_too_large";
   case 429:
      return "too_many_requests";
   case 431:
      return "request_header_fields_too_large";
   case 503:
      return "unavailable";
   case 504:
      return "gateway_timeout";
   default:
      return "internal";
   }
}

status http_status_for(const forge::exceptions::base& error) {
   if (std::string_view{error.code().category().name()} == "forge.http") {
      const auto value = error.code().value();
      if (value >= 400 && value <= 599) {
         return static_cast<status>(value);
      }
   }
   return status::internal_server_error;
}

forge::api::error_payload http_error_payload(const forge::exceptions::base& error) {
   if (std::string_view{error.code().category().name()} == "forge.http") {
      return forge::api::error_payload{
          .error = http_error_name(error.code().value()),
          .message = error.message().empty() ? http_error_name(error.code().value()) : error.message(),
          .retryable = error.code().value() == 429 || error.code().value() == 503 || error.code().value() == 504,
          .identity =
              {
                  .category = error.code().category().name(),
                  .code = static_cast<std::uint32_t>(error.code().value()),
              },
      };
   }

   return forge::api::error_payload{
       .error = "internal",
       .message = "internal error",
       .retryable = false,
       .identity =
           {
               .category = "forge.http",
               .code = static_cast<std::uint32_t>(status::internal_server_error),
           },
   };
}

struct stream_transfer_framing {
   std::optional<std::string> content_length;
   std::optional<std::string> transfer_encoding;
};

std::optional<std::string> header_value(const response& value, field name) {
   if (const auto found = value.find(name); found != value.end()) {
      return std::string{found->value()};
   }
   return std::nullopt;
}

stream_transfer_framing capture_stream_transfer_framing(const response& value) {
   return stream_transfer_framing{
      .content_length = header_value(value, field::content_length),
      .transfer_encoding = header_value(value, field::transfer_encoding),
   };
}

void restore_stream_transfer_framing(response& value, const stream_transfer_framing& framing) {
   value.erase(field::content_length);
   value.erase(field::transfer_encoding);
   if (framing.content_length.has_value()) {
      value.set(field::content_length, *framing.content_length);
   }
   if (framing.transfer_encoding.has_value()) {
      value.set(field::transfer_encoding, *framing.transfer_encoding);
   }
}

std::string encode_error_payload(const forge::api::error_payload& payload) {
   auto encoded = forge::json::write(payload);
   if (encoded.ok()) {
      return std::move(encoded.text);
   }
   return forge::json::write(forge::api::make_internal_error_payload()).text;
}

response make_exception_response(const request& request, const forge::exceptions::base& error) {
   return make_text_response(request, http_status_for(error), encode_error_payload(http_error_payload(error)),
                             "application/json");
}

std::vector<std::string> split_route_path(const std::string& path) {
   if (path.empty() || path.front() != '/') {
      throw std::invalid_argument{"route path must start with /"};
   }
   if (path == "/") {
      return {};
   }

   auto segments = std::vector<std::string>{};
   auto start = std::size_t{1};
   while (start <= path.size()) {
      const auto separator = path.find('/', start);
      const auto end = separator == std::string::npos ? path.size() : separator;
      segments.push_back(path.substr(start, end - start));
      if (separator == std::string::npos) {
         break;
      }
      start = separator + 1U;
   }
   return segments;
}

bool parameter_segment(const std::string& segment) {
   return segment.size() > 1U && segment.front() == ':';
}

bool parameterized(const std::vector<std::string>& segments) {
   for (const auto& segment : segments) {
      if (parameter_segment(segment)) {
         return true;
      }
   }
   return false;
}

template <typename Entry>
bool match_path(const Entry& entry, const target& parsed_target, std::unordered_map<std::string, std::string>* params) {
   if (entry.segments.size() != parsed_target.segments.size()) {
      return false;
   }

   auto captured = std::unordered_map<std::string, std::string>{};
   for (auto index = std::size_t{0}; index != entry.segments.size(); ++index) {
      const auto& pattern = entry.segments[index];
      const auto& value = parsed_target.segments[index];
      if (parameter_segment(pattern)) {
         captured.emplace(pattern.substr(1), value);
         continue;
      }
      if (pattern != value) {
         return false;
      }
   }

   if (params != nullptr) {
      *params = std::move(captured);
   }
   return true;
}

template <typename Entry> bool path_exists(const std::vector<Entry>& entries, const target& parsed_target) {
   for (const auto& entry : entries) {
      if (match_path(entry, parsed_target, nullptr)) {
         return true;
      }
   }
   return false;
}

template <typename Entry>
bool method_path_exists(const std::vector<Entry>& entries, method verb, const target& parsed_target) {
   for (const auto& entry : entries) {
      if (entry.verb == verb && match_path(entry, parsed_target, nullptr)) {
         return true;
      }
   }
   return false;
}

bool path_prefix_matches(const std::string& prefix, const target& parsed_target) {
   if (prefix.empty() || prefix == "/") {
      return true;
   }
   if (!parsed_target.path.starts_with(prefix)) {
      return false;
   }
   return parsed_target.path.size() == prefix.size() || prefix.back() == '/' || parsed_target.path[prefix.size()] == '/';
}

middleware_list matching_middlewares(const std::vector<middleware_descriptor>& middlewares, const target& parsed_target) {
   auto result = middleware_list{};
   for (const auto& descriptor : middlewares) {
      if (path_prefix_matches(descriptor.path_prefix, parsed_target)) {
         result.push_back(descriptor.handler);
      }
   }
   return result;
}

template <typename Entry>
const Entry* find_path_match(const std::vector<Entry>& entries, const target& parsed_target,
                             std::unordered_map<std::string, std::string>& params) {
   for (const auto prefer_parameterized : {false, true}) {
      for (const auto& entry : entries) {
         if (entry.parameterized != prefer_parameterized) {
            continue;
         }
         if (match_path(entry, parsed_target, &params)) {
            return &entry;
         }
      }
   }
   return nullptr;
}

} // namespace

void router::use(middleware handler) {
   use(middleware_descriptor{
       .id = "__anonymous_" + std::to_string(++anonymous_middleware_id_),
       .phase = middleware_phase::before_handler,
       .order = static_cast<int>(anonymous_middleware_id_),
       .path_prefix = "/",
       .handler = std::move(handler),
   });
}

void router::use(middleware_descriptor descriptor) {
   if (!descriptor.handler) {
      throw exceptions::bad_request{"HTTP middleware handler must not be empty"};
   }
   if (descriptor.id.empty()) {
      descriptor.id = "__anonymous_" + std::to_string(++anonymous_middleware_id_);
   }
   if (descriptor.path_prefix.empty()) {
      descriptor.path_prefix = "/";
   }
   for (const auto& existing : middlewares_) {
      if (existing.id == descriptor.id) {
         throw exceptions::conflict{"duplicate HTTP middleware id"};
      }
   }
   middlewares_.push_back(std::move(descriptor));
   std::sort(middlewares_.begin(), middlewares_.end(), [](const auto& left, const auto& right) {
      if (left.phase != right.phase) {
         return static_cast<int>(left.phase) < static_cast<int>(right.phase);
      }
      if (left.order != right.order) {
         return left.order < right.order;
      }
      return left.id < right.id;
   });
}

void router::get(std::string path, route_handler handler) {
   add_route(method::get, std::move(path), std::move(handler));
}

void router::head(std::string path, route_handler handler) {
   add_route(method::head, std::move(path), std::move(handler));
}

void router::post(std::string path, route_handler handler) {
   add_route(method::post, std::move(path), std::move(handler));
}

void router::put(std::string path, route_handler handler) {
   add_route(method::put, std::move(path), std::move(handler));
}

void router::patch(std::string path, route_handler handler) {
   add_route(method::patch, std::move(path), std::move(handler));
}

void router::del(std::string path, route_handler handler) {
   add_route(method::delete_, std::move(path), std::move(handler));
}

void router::get_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::get, std::move(path), std::move(handler));
}

void router::head_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::head, std::move(path), std::move(handler));
}

void router::post_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::post, std::move(path), std::move(handler));
}

void router::put_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::put, std::move(path), std::move(handler));
}

void router::patch_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::patch, std::move(path), std::move(handler));
}

void router::del_stream(std::string path, stream_route_handler handler) {
   add_stream_route(method::delete_, std::move(path), std::move(handler));
}

void router::websocket(std::string path, websocket_route_handler handler) {
   auto segments = split_route_path(path);
   websocket_routes_.push_back(websocket_route_entry{
       .path = std::move(path),
       .segments = segments,
       .parameterized = parameterized(segments),
       .handler = std::move(handler),
   });
}

boost::asio::awaitable<response> router::handle(route_context& context) const {
   try {
      auto params = std::unordered_map<std::string, std::string>{};
      for (const auto prefer_parameterized : {false, true}) {
         for (const auto& route : routes_) {
            if (route.verb != context.request.method() || route.parameterized != prefer_parameterized) {
               continue;
            }
            if (!match_path(route, context.parsed_target, &params)) {
               continue;
            }

            context.route_params = std::move(params);
            co_return co_await run_middleware_chain(matching_middlewares(middlewares_, context.parsed_target), context,
                                                    route.handler);
         }
      }

      if (path_exists(routes_, context.parsed_target)) {
         co_return make_text_response(context.request, status::method_not_allowed, "method not allowed");
      }
      if (path_exists(stream_routes_, context.parsed_target)) {
         co_return make_text_response(context.request, status::method_not_allowed, "method not allowed");
      }
      if (path_exists(websocket_routes_, context.parsed_target)) {
         co_return make_text_response(context.request, status::upgrade_required, "websocket upgrade required");
      }
      co_return make_text_response(context.request, status::not_found, "not found");
   } catch (const forge::exceptions::base& error) {
      co_return make_exception_response(context.request, error);
   } catch (const std::exception&) {
      co_return make_text_response(context.request, status::internal_server_error,
                                   encode_error_payload(forge::api::error_payload{
                                       .error = "internal",
                                       .message = "internal error",
                                       .identity =
                                           {
                                               .category = "forge.http",
                                               .code = static_cast<std::uint32_t>(status::internal_server_error),
                                           },
                                   }),
                                   "application/json");
   } catch (...) {
      co_return make_text_response(context.request, status::internal_server_error, "internal server error");
   }
}

std::optional<response> router::classify_header_only_rejection(route_context& context) const {
   for (const auto& route : routes_) {
      if (route.verb == context.request.method() && match_path(route, context.parsed_target, nullptr)) {
         return std::nullopt;
      }
   }
   for (const auto& route : stream_routes_) {
      if (route.verb == context.request.method() && match_path(route, context.parsed_target, nullptr)) {
         return std::nullopt;
      }
   }

   if (path_exists(routes_, context.parsed_target) || path_exists(stream_routes_, context.parsed_target)) {
      return make_text_response(context.request, status::method_not_allowed, "method not allowed");
   }
   if (path_exists(websocket_routes_, context.parsed_target)) {
      return make_text_response(context.request, status::upgrade_required, "websocket upgrade required");
   }
   return make_text_response(context.request, status::not_found, "not found");
}

bool router::can_handle_stream(route_context& context) const {
   for (const auto& route : stream_routes_) {
      if (route.verb == context.request.method() && match_path(route, context.parsed_target, nullptr)) {
         return true;
      }
   }
   return false;
}

boost::asio::awaitable<stream_response> router::handle_stream(stream_request& request) const {
   auto& context = request.context;
   try {
      auto params = std::unordered_map<std::string, std::string>{};
      for (const auto prefer_parameterized : {false, true}) {
         for (const auto& route : stream_routes_) {
            if (route.verb != context.request.method() || route.parameterized != prefer_parameterized) {
               continue;
            }
            if (!match_path(route, context.parsed_target, &params)) {
               continue;
            }

            context.route_params = std::move(params);
            auto result = std::optional<stream_response>{};
            auto framing = stream_transfer_framing{};
            auto stream_state = stream_pass_through_state{};
            auto head = co_await run_middleware_chain(
               matching_middlewares(middlewares_, context.parsed_target), context,
               [&request, &route, &result, &framing, &stream_state](route_context&) -> boost::asio::awaitable<response> {
                  result = co_await route.handler(request);
                  framing = capture_stream_transfer_framing(result->head);
                  stream_state = mark_stream_pass_through(result->head);
                  co_return std::move(result->head);
               });
            if (!result.has_value()) {
               clear_stream_pass_through(head);
               co_return stream_response::buffered(std::move(head));
            }
            const auto preserve_stream_body =
               static_cast<bool>(result->body) && is_stream_pass_through(head, stream_state) && head.body().empty();
            clear_stream_pass_through(head);
            if (!preserve_stream_body) {
               co_return stream_response::buffered(std::move(head));
            }
            if (result->body) {
               restore_stream_transfer_framing(head, framing);
            }
            result->head = std::move(head);
            co_return std::move(*result);
         }
      }

      if (path_exists(stream_routes_, context.parsed_target)) {
         co_return stream_response::buffered(
            make_text_response(context.request, status::method_not_allowed, "method not allowed"));
      }
      co_return stream_response::buffered(make_text_response(context.request, status::not_found, "not found"));
   } catch (const forge::exceptions::base& error) {
      co_return stream_response::buffered(make_exception_response(context.request, error));
   } catch (const std::exception&) {
      co_return stream_response::buffered(make_text_response(context.request, status::internal_server_error,
                                                            encode_error_payload(forge::api::error_payload{
                                                                .error = "internal",
                                                                .message = "internal error",
                                                                .identity =
                                                                    {
                                                                        .category = "forge.http",
                                                                        .code = static_cast<std::uint32_t>(
                                                                           status::internal_server_error),
                                                                    },
                                                            }),
                                                            "application/json"));
   } catch (...) {
      co_return stream_response::buffered(
         make_text_response(context.request, status::internal_server_error, "internal server error"));
   }
}

std::optional<websocket_route_handler> router::match_websocket(route_context& context) const {
   if (context.request.method() != method::get) {
      return std::nullopt;
   }

   auto params = std::unordered_map<std::string, std::string>{};
   if (const auto* route = find_path_match(websocket_routes_, context.parsed_target, params); route != nullptr) {
      context.route_params = std::move(params);
      return route->handler;
   }
   return std::nullopt;
}

void router::add_route(method verb, std::string path, route_handler handler) {
   auto segments = split_route_path(path);
   for (const auto& route : routes_) {
      if (route.verb == verb && route.path == path) {
         throw exceptions::conflict{"duplicate HTTP route"};
      }
   }
   for (const auto& route : stream_routes_) {
      if (route.verb == verb && route.path == path) {
         throw exceptions::conflict{"duplicate HTTP route"};
      }
   }
   routes_.push_back(route_entry{
       .verb = verb,
       .path = std::move(path),
       .segments = segments,
       .parameterized = parameterized(segments),
       .handler = std::move(handler),
   });
}

void router::add_stream_route(method verb, std::string path, stream_route_handler handler) {
   auto segments = split_route_path(path);
   for (const auto& route : routes_) {
      if (route.verb == verb && route.path == path) {
         throw exceptions::conflict{"duplicate HTTP stream route"};
      }
   }
   for (const auto& route : stream_routes_) {
      if (route.verb == verb && route.path == path) {
         throw exceptions::conflict{"duplicate HTTP stream route"};
      }
   }
   stream_routes_.push_back(stream_route_entry{
       .verb = verb,
       .path = std::move(path),
       .segments = segments,
       .parameterized = parameterized(segments),
       .handler = std::move(handler),
   });
}

} // namespace forge::http
