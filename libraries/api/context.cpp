module;

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

module fcl.api.context;

namespace fcl::api {
namespace {

struct context_frame {
   std::uint64_t token = 0;
   call_context context;
};

std::mutex context_mutex;
std::atomic<std::uint64_t> next_context_token{1};
std::unordered_map<std::thread::id, std::vector<context_frame>> current_context;

} // namespace

call_context_scope::call_context_scope(call_context value) {
   owner_ = std::this_thread::get_id();
   token_ = next_context_token.fetch_add(1, std::memory_order_relaxed);
   auto lock = std::scoped_lock{context_mutex};
   current_context[owner_].push_back(context_frame{
      .token = token_,
      .context = std::move(value),
   });
}

call_context_scope::~call_context_scope() {
   if (!active_) {
      return;
   }

   auto lock = std::scoped_lock{context_mutex};
   auto owner = current_context.find(owner_);
   if (owner == current_context.end()) {
      return;
   }

   auto& stack = owner->second;
   const auto frame = std::find_if(stack.begin(), stack.end(), [token = token_](const auto& item) {
      return item.token == token;
   });
   if (frame != stack.end()) {
      stack.erase(frame);
   }
   if (stack.empty()) {
      current_context.erase(owner);
   }
}

std::optional<call_context> current_call_context() {
   auto lock = std::scoped_lock{context_mutex};
   const auto owner = current_context.find(std::this_thread::get_id());
   if (owner == current_context.end() || owner->second.empty()) {
      return std::nullopt;
   }
   return owner->second.back().context;
}

std::optional<std::string> metadata_value(const metadata& value, std::string_view key) {
   for (const auto& item : value) {
      if (item.key == key) {
         return item.value;
      }
   }
   return std::nullopt;
}

} // namespace fcl::api
