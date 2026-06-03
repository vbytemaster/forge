module;

#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

module fcl.http.router;

import fcl.api;
import fcl.http.exceptions;
import fcl.http.target;

namespace fcl::http {
namespace {

std::string json_escape(std::string_view value) {
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
      default:
         output.push_back(character);
         break;
      }
   }
   return output;
}

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
   case 409:
      return "conflict";
   case 429:
      return "too_many_requests";
   case 503:
      return "unavailable";
   case 504:
      return "gateway_timeout";
   default:
      return "internal";
   }
}

status http_status_for(const fcl::exceptions::base& error) {
   if (std::string_view{error.code().category().name()} == "fcl.http") {
      const auto value = error.code().value();
      if (value >= 400 && value <= 599) {
         return static_cast<status>(value);
      }
   }
   return status::internal_server_error;
}

fcl::api::error_payload http_error_payload(const fcl::exceptions::base& error) {
   if (std::string_view{error.code().category().name()} == "fcl.http") {
      return fcl::api::error_payload{
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

   return fcl::api::error_payload{
       .error = "internal",
       .message = "internal error",
       .retryable = false,
       .identity =
           {
               .category = "fcl.http",
               .code = static_cast<std::uint32_t>(status::internal_server_error),
           },
   };
}

std::string render_error_payload(const fcl::api::error_payload& payload) {
   auto output = std::string{};
   output += "{\"error\":\"";
   output += json_escape(payload.error);
   output += "\",\"message\":\"";
   output += json_escape(payload.message);
   output += "\",\"retryable\":";
   output += payload.retryable ? "true" : "false";
   output += ",\"identity\":{\"category\":\"";
   output += json_escape(payload.identity.category);
   output += "\",\"code\":";
   output += std::to_string(payload.identity.code);
   output += "}}";
   return output;
}

response make_exception_response(const request& request, const fcl::exceptions::base& error) {
   return make_text_response(request, http_status_for(error), render_error_payload(http_error_payload(error)),
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

void router::post(std::string path, route_handler handler) {
   add_route(method::post, std::move(path), std::move(handler));
}

void router::put(std::string path, route_handler handler) {
   add_route(method::put, std::move(path), std::move(handler));
}

void router::del(std::string path, route_handler handler) {
   add_route(method::delete_, std::move(path), std::move(handler));
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

response router::handle(route_context& context) const {
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
            return run_middleware_chain(matching_middlewares(middlewares_, context.parsed_target), context,
                                        route.handler);
         }
      }

      if (path_exists(routes_, context.parsed_target)) {
         return make_text_response(context.request, status::method_not_allowed, "method not allowed");
      }
      if (path_exists(websocket_routes_, context.parsed_target)) {
         return make_text_response(context.request, status::upgrade_required, "websocket upgrade required");
      }
      return make_text_response(context.request, status::not_found, "not found");
   } catch (const fcl::exceptions::base& error) {
      return make_exception_response(context.request, error);
   } catch (const std::exception&) {
      return make_text_response(context.request, status::internal_server_error,
                                render_error_payload(fcl::api::error_payload{
                                    .error = "internal",
                                    .message = "internal error",
                                    .identity =
                                        {
                                            .category = "fcl.http",
                                            .code = static_cast<std::uint32_t>(status::internal_server_error),
                                        },
                                }),
                                "application/json");
   } catch (...) {
      return make_text_response(context.request, status::internal_server_error, "internal server error");
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
   routes_.push_back(route_entry{
       .verb = verb,
       .path = std::move(path),
       .segments = segments,
       .parameterized = parameterized(segments),
       .handler = std::move(handler),
   });
}

} // namespace fcl::http
