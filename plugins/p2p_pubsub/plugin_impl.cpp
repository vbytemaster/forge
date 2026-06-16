module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module fcl.plugins.p2p_pubsub.plugin;

import fcl.exceptions;
import fcl.p2p.identity;
import fcl.p2p.pubsub;
import fcl.plugins.p2p_node.api;
import fcl.plugins.p2p_pubsub.api;
import fcl.plugins.p2p_pubsub.exceptions;
import fcl.plugins.p2p_pubsub.types;

#include "details/join_flow.hxx"
#include "details/message_projection.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::p2p_pubsub {

fcl::plugins::p2p_node::pubsub_source& plugin::impl::require_source() const {
   if (!initialized || !source) {
      FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P PubSub plugin is not initialized");
   }
   return *source;
}

void plugin::impl::ensure_topic_allowed(const fcl::p2p::pubsub::topic& subject) const {
   if (subject.value.empty()) {
      FCL_THROW_EXCEPTION(exceptions::topic_not_allowed, "P2P PubSub topic is empty");
   }
   if (!settings.allowed_topics.empty() && !contains_topic(settings.allowed_topics, subject.value)) {
      FCL_THROW_EXCEPTION(exceptions::topic_not_allowed, "P2P PubSub topic is not allowed",
                          fcl::exceptions::ctx("topic", subject.value));
   }
   if (contains_topic(settings.denied_topics, subject.value)) {
      FCL_THROW_EXCEPTION(exceptions::topic_not_allowed, "P2P PubSub topic is denied",
                          fcl::exceptions::ctx("topic", subject.value));
   }
}

std::vector<handler_record> plugin::impl::handlers_for(const std::string& topic) const {
   auto lock = std::scoped_lock{mutex};
   auto out = std::vector<handler_record>{};
   if (const auto found = topics.find(topic); found != topics.end()) {
      out.reserve(found->second.handlers.size());
      for (const auto& [_, handler] : found->second.handlers) {
         out.push_back(handler);
      }
   }
   return out;
}

bool plugin::impl::try_begin_handler() {
   auto lock = std::scoped_lock{mutex};
   if (active_handlers >= settings.max_active_handlers) {
      ++messages_dropped;
      return false;
   }
   ++active_handlers;
   return true;
}

void plugin::impl::finish_handler() {
   auto lock = std::scoped_lock{mutex};
   if (active_handlers > 0) {
      --active_handlers;
   }
}

void plugin::impl::record_handler_failure() {
   auto lock = std::scoped_lock{mutex};
   ++handler_failures;
}

void plugin::impl::record_drop() {
   auto lock = std::scoped_lock{mutex};
   ++messages_dropped;
}

boost::asio::awaitable<fcl::p2p::pubsub::validation_result>
plugin::impl::call_handler(handler_record handler, message value) {
   if (!try_begin_handler()) {
      co_return fcl::p2p::pubsub::validation_result::ignore;
   }

   if (handler.deadline.count() <= 0) {
      try {
         auto result = co_await handler.callback(std::move(value));
         finish_handler();
         co_return result;
      } catch (...) {
         finish_handler();
         record_handler_failure();
         co_return fcl::p2p::pubsub::validation_result::ignore;
      }
   }

   auto executor = co_await boost::asio::this_coro::executor;
   auto self = shared_from_this();
   auto guarded_handler =
      [self, handler = std::move(handler), value = std::move(value)]() mutable
      -> boost::asio::awaitable<std::optional<fcl::p2p::pubsub::validation_result>> {
      try {
         co_return co_await handler.callback(std::move(value));
      } catch (...) {
         self->record_handler_failure();
         co_return std::nullopt;
      }
   };
   auto timer = boost::asio::steady_timer{executor};
   timer.expires_after(handler.deadline);

   try {
      using namespace boost::asio::experimental::awaitable_operators;
      auto result = co_await (guarded_handler() || timer.async_wait(boost::asio::use_awaitable));
      finish_handler();
      if (result.index() == 0) {
         const auto& value = std::get<0>(result);
         if (value.has_value()) {
            co_return *value;
         }
         co_return fcl::p2p::pubsub::validation_result::ignore;
      }
      record_handler_failure();
   } catch (...) {
      finish_handler();
      record_handler_failure();
   }
   co_return fcl::p2p::pubsub::validation_result::ignore;
}

boost::asio::awaitable<fcl::p2p::pubsub::validation_result>
plugin::impl::handle_event(fcl::p2p::pubsub::event event) {
   if (event.value.data.size() > settings.max_message_size) {
      record_drop();
      co_return fcl::p2p::pubsub::validation_result::ignore;
   }

   auto handlers = handlers_for(event.value.subject.value);
   if (handlers.empty()) {
      co_return fcl::p2p::pubsub::validation_result::ignore;
   }

   auto final_result = fcl::p2p::pubsub::validation_result::ignore;
   for (auto& handler : handlers) {
      auto result = co_await call_handler(handler, project_message(event.source, event.value));
      if (result == fcl::p2p::pubsub::validation_result::reject) {
         final_result = fcl::p2p::pubsub::validation_result::reject;
      } else if (result == fcl::p2p::pubsub::validation_result::accept &&
                 final_result != fcl::p2p::pubsub::validation_result::reject) {
         final_result = fcl::p2p::pubsub::validation_result::accept;
      }
   }

   auto lock = std::scoped_lock{mutex};
   ++messages_delivered;
   switch (final_result) {
   case fcl::p2p::pubsub::validation_result::accept:
      ++messages_accepted;
      break;
   case fcl::p2p::pubsub::validation_result::reject:
      ++messages_rejected;
      break;
   case fcl::p2p::pubsub::validation_result::ignore:
      ++messages_ignored;
      break;
   }
   co_return final_result;
}

} // namespace fcl::plugins::p2p_pubsub
