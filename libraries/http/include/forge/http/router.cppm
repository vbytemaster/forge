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
   [[nodiscard]] std::optional<response> classify_header_only_rejection(route_context& context) const;
   [[nodiscard]] boost::asio::awaitable<stream_response> handle_stream(stream_request& request) const;
   [[nodiscard]] std::optional<websocket_route_handler> match_websocket(route_context& context) const;

 private:
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
