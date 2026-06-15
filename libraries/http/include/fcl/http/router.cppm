module;

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.http.router;

import fcl.http.middleware;
import fcl.http.route_context;
import fcl.http.types;
import fcl.websocket.connection;

export namespace fcl::http {

using websocket_route_handler = std::function<void(std::shared_ptr<fcl::websocket::connection>)>;

class router {
   public:
      void use(middleware handler);
      void use(middleware_descriptor descriptor);

   void get(std::string path, route_handler handler);
   void post(std::string path, route_handler handler);
   void put(std::string path, route_handler handler);
   void del(std::string path, route_handler handler);
   void websocket(std::string path, websocket_route_handler handler);

   template <typename Binding> void mount(const Binding& binding) {
      binding.mount(*this);
   }

   [[nodiscard]] boost::asio::awaitable<response> handle(route_context& context) const;
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

   void add_route(method verb, std::string path, route_handler handler);

   std::vector<route_entry> routes_;
   std::vector<websocket_route_entry> websocket_routes_;
   std::vector<middleware_descriptor> middlewares_;
   std::uint64_t anonymous_middleware_id_ = 0;
};

} // namespace fcl::http
