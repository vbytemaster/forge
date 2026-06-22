module;

#include <functional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.http.middleware;

import fcl.http.route_context;
import fcl.http.types;

export namespace fcl::http {

using route_handler = std::function<boost::asio::awaitable<response>(route_context&)>;
using next_handler = std::function<boost::asio::awaitable<response>()>;
using middleware = std::function<boost::asio::awaitable<response>(route_context&, next_handler)>;
using middleware_list = std::vector<middleware>;

enum class middleware_phase {
   request_context = 1,
   security = 2,
   limits = 3,
   before_handler = 4,
   after_handler = 5,
   error = 6,
};

struct middleware_descriptor {
   std::string id;
   middleware_phase phase = middleware_phase::before_handler;
   int order = 0;
   std::string path_prefix = "/";
   middleware handler;
};

class stream_pass_through_state {
 public:
   [[nodiscard]] bool present() const noexcept {
      return !token_.empty();
   }

   void clear() noexcept {
      token_.clear();
   }

 private:
   std::string token_;

   friend stream_pass_through_state mark_stream_pass_through(response& value);
   friend stream_pass_through_state capture_stream_pass_through(response& value);
   friend bool is_stream_pass_through(const response& value, const stream_pass_through_state& state);
   friend void restore_stream_pass_through(response& value, const stream_pass_through_state& state);
};

[[nodiscard]] stream_pass_through_state mark_stream_pass_through(response& value);
[[nodiscard]] stream_pass_through_state capture_stream_pass_through(response& value);
[[nodiscard]] bool is_stream_pass_through(const response& value, const stream_pass_through_state& state);
void restore_stream_pass_through(response& value, const stream_pass_through_state& state);
void clear_stream_pass_through(response& value);

boost::asio::awaitable<response> run_middleware_chain(middleware_list middlewares, route_context& context,
                                                      route_handler terminal);

} // namespace fcl::http
