module;

#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module forge.http.router;

import forge.http.middleware;
import forge.http.route_context;
import forge.http.stream;
import forge.http.target;
import forge.http.types;
import forge.websocket.connection;

export namespace forge::http {

using websocket_route_handler = std::function<void(std::shared_ptr<forge::websocket::connection>)>;

class router;

class router {
 public:
   void use(middleware handler);
   void use(middleware_descriptor descriptor);

   void get(std::string path, route_handler handler);
   void head(std::string path, route_handler handler);
   void post(std::string path, route_handler handler);
   void put(std::string path, route_handler handler);
   void patch(std::string path, route_handler handler);
   void del(std::string path, route_handler handler);
   void get_stream(std::string path, stream_route_handler handler);
   void head_stream(std::string path, stream_route_handler handler);
   void post_stream(std::string path, stream_route_handler handler);
   void put_stream(std::string path, stream_route_handler handler);
   void patch_stream(std::string path, stream_route_handler handler);
   void del_stream(std::string path, stream_route_handler handler);
   void websocket(std::string path, websocket_route_handler handler);

   template <typename Binding> void mount(const Binding& binding) {
      binding.mount(*this);
   }

   [[nodiscard]] boost::asio::awaitable<response> handle(route_context& context) const;
   [[nodiscard]] bool can_handle_stream(route_context& context) const;
   [[nodiscard]] boost::asio::awaitable<stream_response> handle_stream(stream_request& request) const;
   [[nodiscard]] std::optional<websocket_route_handler> match_websocket(route_context& context) const;

 private:
   friend std::optional<response> router_preflight(const router& router_value, route_context& context) {
      const auto parameter_segment = [](const std::string& segment) {
         return segment.size() > 1U && segment.front() == ':';
      };
      const auto match_path = [&parameter_segment](const auto& entry, const target& parsed_target) {
         if (entry.segments.size() != parsed_target.segments.size()) {
            return false;
         }
         for (auto index = std::size_t{0}; index != entry.segments.size(); ++index) {
            const auto& pattern = entry.segments[index];
            const auto& value = parsed_target.segments[index];
            if (parameter_segment(pattern)) {
               continue;
            }
            if (pattern != value) {
               return false;
            }
         }
         return true;
      };
      const auto path_exists = [&match_path, &context](const auto& entries) {
         for (const auto& entry : entries) {
            if (match_path(entry, context.parsed_target)) {
               return true;
            }
         }
         return false;
      };
      const auto method_path_exists = [&match_path, &context](const auto& entries) {
         for (const auto& entry : entries) {
            if (entry.verb == context.request.method() && match_path(entry, context.parsed_target)) {
               return true;
            }
         }
         return false;
      };

      if (method_path_exists(router_value.routes_) || method_path_exists(router_value.stream_routes_)) {
         return std::nullopt;
      }
      if (path_exists(router_value.routes_) || path_exists(router_value.stream_routes_)) {
         return make_text_response(context.request, status::method_not_allowed, "method not allowed");
      }
      if (path_exists(router_value.websocket_routes_)) {
         return make_text_response(context.request, status::upgrade_required, "websocket upgrade required");
      }
      return make_text_response(context.request, status::not_found, "not found");
   }

   struct route_entry {
      method verb;
      std::string path;
      std::vector<std::string> segments;
      bool parameterized = false;
      route_handler handler;
   };

   struct websocket_route_entry {
      std::string path;
      std::vector<std::string> segments;
      bool parameterized = false;
      websocket_route_handler handler;
   };

   struct stream_route_entry {
      method verb;
      std::string path;
      std::vector<std::string> segments;
      bool parameterized = false;
      stream_route_handler handler;
   };

   void add_route(method verb, std::string path, route_handler handler);
   void add_stream_route(method verb, std::string path, stream_route_handler handler);

   std::vector<route_entry> routes_;
   std::vector<websocket_route_entry> websocket_routes_;
   std::vector<stream_route_entry> stream_routes_;
   std::vector<middleware_descriptor> middlewares_;
   std::uint64_t anonymous_middleware_id_ = 0;
};

} // namespace forge::http
