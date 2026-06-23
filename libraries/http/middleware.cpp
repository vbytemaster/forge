module;

#include <functional>
#include <coroutine>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

module forge.http.middleware;

namespace forge::http {
namespace {

constexpr std::string_view stream_token_header = "X-FORGE-Stream-Token";

std::string make_stream_token() {
   static auto next_token = std::atomic<std::uint64_t>{0};
   return std::to_string(next_token.fetch_add(1, std::memory_order_relaxed) + 1U);
}

} // namespace

stream_pass_through_state mark_stream_pass_through(response& value) {
   auto state = stream_pass_through_state{};
   state.token_ = make_stream_token();
   value.set(stream_token_header, state.token_);
   return state;
}

stream_pass_through_state capture_stream_pass_through(response& value) {
   auto state = stream_pass_through_state{};
   if (const auto found = value.find(stream_token_header); found != value.end()) {
      state.token_ = std::string{found->value()};
      value.erase(stream_token_header);
   }
   return state;
}

bool is_stream_pass_through(const response& value, const stream_pass_through_state& state) {
   if (!state.present()) {
      return false;
   }
   const auto found = value.find(stream_token_header);
   return found != value.end() && found->value() == state.token_;
}

void restore_stream_pass_through(response& value, const stream_pass_through_state& state) {
   if (state.present()) {
      value.set(stream_token_header, state.token_);
   }
}

void clear_stream_pass_through(response& value) {
   value.erase(stream_token_header);
}

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

} // namespace forge::http
