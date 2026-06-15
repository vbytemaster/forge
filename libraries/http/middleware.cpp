module;

#include <functional>
#include <coroutine>

#include <boost/asio/awaitable.hpp>

module fcl.http.middleware;

namespace fcl::http {

boost::asio::awaitable<response> run_middleware_chain(middleware_list middlewares, route_context& context,
                                                      route_handler terminal) {
   auto invoke = std::function<boost::asio::awaitable<response>(std::size_t)>{};
   invoke = [&](std::size_t index) -> boost::asio::awaitable<response> {
      if (index == middlewares.size()) {
         co_return co_await terminal(context);
      }
      co_return co_await middlewares[index](context, [&invoke, next_index = index + 1U]() { return invoke(next_index); });
   };

   co_return co_await invoke(0);
}

} // namespace fcl::http
