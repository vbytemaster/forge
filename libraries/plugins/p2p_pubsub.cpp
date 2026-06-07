module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module fcl.plugins.p2p_pubsub;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.config.component;
import fcl.config.decode;
import fcl.exceptions;
import fcl.p2p;
import fcl.plugins.p2p_node;

namespace fcl::plugins {
namespace {

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

[[nodiscard]] p2p_pubsub::config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<p2p_pubsub::config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P PubSub config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      FCL_THROW_EXCEPTION(p2p_pubsub::exceptions::invalid_config, message);
   }
   return std::move(decoded.value);
}

void validate_topic_list(const std::vector<std::string>& values, std::string_view name) {
   auto seen = std::set<std::string>{};
   for (const auto& value : values) {
      if (value.empty()) {
         FCL_THROW_EXCEPTION(p2p_pubsub::exceptions::invalid_config, "P2P PubSub topic policy contains empty topic",
                             fcl::exceptions::ctx("list", std::string{name}));
      }
      if (!seen.insert(value).second) {
         FCL_THROW_EXCEPTION(p2p_pubsub::exceptions::invalid_config,
                             "P2P PubSub topic policy contains duplicate topic",
                             fcl::exceptions::ctx("list", std::string{name}), fcl::exceptions::ctx("topic", value));
      }
   }
}

void validate_config(const p2p_pubsub::config& value) {
   if (value.max_topics == 0 || value.max_handlers_per_topic == 0 || value.max_active_handlers == 0 ||
       value.max_message_size == 0 || value.handler_deadline_ms == 0) {
      FCL_THROW_EXCEPTION(p2p_pubsub::exceptions::invalid_config, "P2P PubSub limits must be positive");
   }
   validate_topic_list(value.allowed_topics, "allowed-topics");
   validate_topic_list(value.denied_topics, "denied-topics");
}

[[nodiscard]] bool contains_topic(const std::vector<std::string>& values, const std::string& topic) {
   return std::ranges::find(values, topic) != values.end();
}

[[nodiscard]] p2p_pubsub::message project_message(const fcl::p2p::peer_id& source,
                                                  const fcl::p2p::pubsub::message& value) {
   return p2p_pubsub::message{
      .source = source,
      .author = value.from,
      .subject = value.subject,
      .data = value.data,
      .seqno = value.seqno,
   };
}

[[nodiscard]] fcl::p2p::pubsub::options core_options_for(const p2p_pubsub::config& settings) {
   auto out = fcl::p2p::pubsub::options{};
   out.signatures =
      settings.sign_publishes ? fcl::p2p::pubsub::signature_policy::strict_sign
                              : fcl::p2p::pubsub::signature_policy::lax_no_sign;
   out.limits.max_data_size = static_cast<std::size_t>(settings.max_message_size);
   out.limits.max_message_size = static_cast<std::size_t>(settings.max_message_size) + 1024;
   out.limits.max_topics = static_cast<std::size_t>(settings.max_topics);
   out.limits.max_validation_queue = static_cast<std::size_t>(settings.max_active_handlers);
   return out;
}

} // namespace

struct p2p_pubsub::impl : public std::enable_shared_from_this<p2p_pubsub::impl> {
   struct handler_record {
      std::uint64_t id = 0;
      fcl::p2p::pubsub::topic subject;
      p2p_pubsub::handler callback;
      std::chrono::milliseconds deadline{0};
   };

   struct topic_state {
      std::map<std::uint64_t, handler_record> handlers;
   };

   config settings;
   std::shared_ptr<fcl::plugins::p2p_node::pubsub_source> source;
   std::map<std::string, topic_state> topics;
   std::uint64_t next_subscription = 1;
   std::size_t active_handlers = 0;
   std::uint64_t messages_published = 0;
   std::uint64_t messages_delivered = 0;
   std::uint64_t messages_accepted = 0;
   std::uint64_t messages_rejected = 0;
   std::uint64_t messages_ignored = 0;
   std::uint64_t messages_dropped = 0;
   std::uint64_t handler_failures = 0;
   mutable std::mutex mutex;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] fcl::plugins::p2p_node::pubsub_source& require_source() const {
      if (!initialized || !source) {
         FCL_THROW_EXCEPTION(exceptions::plugin_not_initialized, "P2P PubSub plugin is not initialized");
      }
      return *source;
   }

   void ensure_topic_allowed(const fcl::p2p::pubsub::topic& subject) const {
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

   [[nodiscard]] std::vector<handler_record> handlers_for(const std::string& topic) const {
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

   [[nodiscard]] bool try_begin_handler() {
      auto lock = std::scoped_lock{mutex};
      if (active_handlers >= settings.max_active_handlers) {
         ++messages_dropped;
         return false;
      }
      ++active_handlers;
      return true;
   }

   void finish_handler() {
      auto lock = std::scoped_lock{mutex};
      if (active_handlers > 0) {
         --active_handlers;
      }
   }

   void record_handler_failure() {
      auto lock = std::scoped_lock{mutex};
      ++handler_failures;
   }

   void record_drop() {
      auto lock = std::scoped_lock{mutex};
      ++messages_dropped;
   }

   boost::asio::awaitable<fcl::p2p::pubsub::validation_result>
   call_handler(handler_record handler, p2p_pubsub::message value) {
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

   boost::asio::awaitable<fcl::p2p::pubsub::validation_result> handle_event(fcl::p2p::pubsub::event event) {
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
};

class p2p_pubsub::api::impl final : public p2p_pubsub::api {
 public:
   explicit impl(std::shared_ptr<p2p_pubsub::impl> impl) : impl_{std::move(impl)} {}

   boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                                           publish_options options) override {
      auto& source = impl_->require_source();
      impl_->ensure_topic_allowed(subject);
      if (data.size() > impl_->settings.max_message_size) {
         FCL_THROW_EXCEPTION(exceptions::message_too_large, "P2P PubSub message exceeds configured limit",
                             fcl::exceptions::ctx("topic", subject.value));
      }
      auto published = co_await source.async_publish_message(
         std::move(subject), std::move(data),
         fcl::p2p::pubsub::publish_options{.sign = options.sign.value_or(impl_->settings.sign_publishes)});
      {
         auto lock = std::scoped_lock{impl_->mutex};
         ++impl_->messages_published;
      }
      co_return project_message(source.local_peer(), published);
   }

   boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, handler callback,
                                                  subscribe_options options) override {
      auto& source = impl_->require_source();
      impl_->ensure_topic_allowed(subject);
      if (!callback) {
         FCL_THROW_EXCEPTION(exceptions::handler_limit, "P2P PubSub subscription requires handler");
      }

      auto value = subscription{};
      auto first_for_topic = false;
      {
         auto lock = std::scoped_lock{impl_->mutex};
         const auto new_topic = !impl_->topics.contains(subject.value);
         if (new_topic && impl_->topics.size() >= impl_->settings.max_topics) {
            FCL_THROW_EXCEPTION(exceptions::handler_limit, "P2P PubSub topic limit reached",
                                fcl::exceptions::ctx("topic", subject.value));
         }
         auto& state = impl_->topics[subject.value];
         if (state.handlers.size() >= impl_->settings.max_handlers_per_topic) {
            FCL_THROW_EXCEPTION(exceptions::handler_limit, "P2P PubSub handler limit reached",
                                fcl::exceptions::ctx("topic", subject.value));
         }
         first_for_topic = state.handlers.empty();
         value = subscription{.id = impl_->next_subscription++, .subject = subject};
         auto deadline = options.handler_deadline;
         if (deadline.count() <= 0) {
            deadline = to_ms(impl_->settings.handler_deadline_ms);
         }
         state.handlers.emplace(value.id, p2p_pubsub::impl::handler_record{
                                             .id = value.id,
                                             .subject = subject,
                                             .callback = std::move(callback),
                                             .deadline = deadline,
                                          });
      }

      if (first_for_topic) {
         auto self = impl_;
         try {
            (void)co_await source.async_join_topic(
               subject, [self](fcl::p2p::pubsub::event event) mutable
                           -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
                  co_return co_await self->handle_event(std::move(event));
               });
         } catch (...) {
            auto lock = std::scoped_lock{impl_->mutex};
            if (auto found = impl_->topics.find(value.subject.value); found != impl_->topics.end()) {
               found->second.handlers.erase(value.id);
               if (found->second.handlers.empty()) {
                  impl_->topics.erase(found);
               }
            }
            throw;
         }
      }
      co_return value;
   }

   boost::asio::awaitable<void> unsubscribe(subscription value) override {
      auto& source = impl_->require_source();
      auto last_for_topic = false;
      {
         auto lock = std::scoped_lock{impl_->mutex};
         auto found = impl_->topics.find(value.subject.value);
         if (found == impl_->topics.end() || !found->second.handlers.erase(value.id)) {
            FCL_THROW_EXCEPTION(exceptions::subscription_not_found, "P2P PubSub subscription was not found",
                                fcl::exceptions::ctx("topic", value.subject.value));
         }
         last_for_topic = found->second.handlers.empty();
         if (last_for_topic) {
            impl_->topics.erase(found);
         }
      }
      if (last_for_topic) {
         co_await source.async_leave_topic(std::move(value.subject));
      }
   }

   std::vector<subscription> subscriptions() const override {
      (void)impl_->require_source();
      auto lock = std::scoped_lock{impl_->mutex};
      auto out = std::vector<subscription>{};
      for (const auto& [_, topic] : impl_->topics) {
         for (const auto& [id, handler] : topic.handlers) {
            out.push_back(subscription{.id = id, .subject = handler.subject});
         }
      }
      return out;
   }

   p2p_pubsub::snapshot snapshot() const override {
      auto& source = impl_->require_source();
      auto lock = std::scoped_lock{impl_->mutex};
      auto subscriptions = std::size_t{};
      for (const auto& [_, topic] : impl_->topics) {
         subscriptions += topic.handlers.size();
      }
      return p2p_pubsub::snapshot{
         .topics = impl_->topics.size(),
         .subscriptions = subscriptions,
         .active_handlers = impl_->active_handlers,
         .messages_published = impl_->messages_published,
         .messages_delivered = impl_->messages_delivered,
         .messages_accepted = impl_->messages_accepted,
         .messages_rejected = impl_->messages_rejected,
         .messages_ignored = impl_->messages_ignored,
         .messages_dropped = impl_->messages_dropped,
         .handler_failures = impl_->handler_failures,
         .core = source.snapshot(),
      };
   }

 private:
   std::shared_ptr<p2p_pubsub::impl> impl_;
};

fcl::api::descriptor p2p_pubsub::api::describe() {
   return fcl::api::contract<p2p_pubsub::api>(
             {.id = {"fcl.plugins.p2p_pubsub"}, .version = {.major = 1, .revision = 0}})
      .build();
}

p2p_pubsub::p2p_pubsub() : impl_{std::make_shared<impl>()} {}
p2p_pubsub::~p2p_pubsub() = default;

fcl::app::plugin_id p2p_pubsub::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_pubsub"};
}

std::string p2p_pubsub::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> p2p_pubsub::describe_config() const {
   return fcl::config::describe_component<p2p_pubsub::config>("p2p-pubsub");
}

boost::asio::awaitable<void> p2p_pubsub::configure(fcl::config::component_view view) {
   auto config = decode_config(view);
   validate_config(config);
   impl_->settings = std::move(config);
   co_return;
}

boost::asio::awaitable<void> p2p_pubsub::provide(fcl::api::provider& provider) {
   provider.install<p2p_pubsub::api>(p2p_pubsub::api::describe(), std::make_shared<p2p_pubsub::api::impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> p2p_pubsub::initialize(fcl::app::plugin_context& context) {
   impl_->source = context.apis()
                      .get<fcl::plugins::p2p_node::pubsub_source>(
                         {.id = {"fcl.plugins.p2p_node.pubsub_source"}, .major = 1, .min_revision = 0})
                      .shared();
   impl_->source->enable(core_options_for(impl_->settings));
   impl_->initialized = true;
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> p2p_pubsub::startup() {
   co_return;
}

void p2p_pubsub::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> p2p_pubsub::shutdown() {
   request_stop();
   std::vector<fcl::p2p::pubsub::topic> topics;
   {
      auto lock = std::scoped_lock{impl_->mutex};
      topics.reserve(impl_->topics.size());
      for (const auto& [topic, _] : impl_->topics) {
         topics.push_back(fcl::p2p::pubsub::topic{.value = topic});
      }
      impl_->topics.clear();
   }
   if (impl_->source) {
      for (auto& topic : topics) {
         try {
            co_await impl_->source->async_leave_topic(std::move(topic));
         } catch (...) {
            fcl::exceptions::capture_and_log("P2P PubSub unsubscribe during shutdown failed");
         }
      }
   }
   impl_->initialized = false;
   impl_->source = nullptr;
   co_return;
}

fcl::app::plugin_descriptor p2p_pubsub::descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_pubsub"},
      .dependencies = {fcl::app::plugin_id{.value = "fcl.p2p_node"}},
      .factory = [] {
         return std::make_unique<p2p_pubsub>();
      },
   };
}

} // namespace fcl::plugins
