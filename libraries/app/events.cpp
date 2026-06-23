module;

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.app.events;

namespace forge::app {
namespace {

[[nodiscard]] bool severity_at_least(event_severity value, event_severity minimum) noexcept {
   return static_cast<std::uint8_t>(value) >= static_cast<std::uint8_t>(minimum);
}

[[nodiscard]] bool topic_matches(const event_record& record, const event_filter& filter) {
   if (filter.topic.empty()) {
      return true;
   }
   if (record.topic == filter.topic) {
      return true;
   }
   return filter.include_child_topics && record.topic.size() > filter.topic.size() &&
          record.topic.starts_with(filter.topic) && record.topic[filter.topic.size()] == '.';
}

} // namespace

struct event_subscription::state {
   event_filter filter;
   mutable std::mutex mutex;
   std::deque<event_record> queue;
   bool active = true;
};

struct event_bus::impl {
   explicit impl(event_bus_options options_value) : options{options_value} {}

   event_bus_options options;
   mutable std::mutex mutex;
   std::uint64_t next_id = 1;
   std::vector<std::weak_ptr<event_subscription::state>> subscriptions;
   std::deque<event_record> critical;
   std::deque<event_record> recent;
   event_bus_metrics metrics;
};

event_subscription::event_subscription() = default;
event_subscription::~event_subscription() {
   unsubscribe();
}

event_subscription::event_subscription(std::shared_ptr<state> state) : state_{std::move(state)} {}

event_subscription::event_subscription(event_subscription&&) noexcept = default;
event_subscription& event_subscription::operator=(event_subscription&&) noexcept = default;

std::optional<event_record> event_subscription::poll() {
   if (!state_) {
      return std::nullopt;
   }
   auto lock = std::scoped_lock{state_->mutex};
   if (!state_->active || state_->queue.empty()) {
      return std::nullopt;
   }
   auto value = std::move(state_->queue.front());
   state_->queue.pop_front();
   return value;
}

void event_subscription::unsubscribe() {
   if (state_) {
      auto state = state_;
      {
         auto lock = std::scoped_lock{state->mutex};
         state->active = false;
      }
      state_.reset();
   }
}

event_bus::event_bus(event_bus_options options) : impl_{std::make_shared<impl>(options)} {}

event_bus::~event_bus() = default;

event_subscription event_bus::subscribe(event_filter filter) {
   auto state = std::make_shared<event_subscription::state>();
   state->filter = std::move(filter);

   auto lock = std::scoped_lock{impl_->mutex};
   impl_->subscriptions.push_back(state);
   ++impl_->metrics.subscriptions;
   return event_subscription{std::move(state)};
}

void event_bus::publish(event_severity severity, std::string topic, std::string message) {
   auto record = event_record{
       .id = 0,
       .time = std::chrono::system_clock::now(),
       .severity = severity,
       .topic = std::move(topic),
       .message = std::move(message),
   };

   auto lock = std::scoped_lock{impl_->mutex};
   record.id = impl_->next_id++;
   ++impl_->metrics.published;

   if (severity == event_severity::critical && impl_->options.max_critical_events != 0) {
      if (impl_->critical.size() >= impl_->options.max_critical_events) {
         impl_->critical.pop_front();
      }
      impl_->critical.push_back(record);
   }
   if (impl_->options.max_recent_events != 0) {
      if (impl_->recent.size() >= impl_->options.max_recent_events) {
         impl_->recent.pop_front();
      }
      impl_->recent.push_back(record);
   }

   auto live = std::vector<std::weak_ptr<event_subscription::state>>{};
   live.reserve(impl_->subscriptions.size());
   for (const auto& weak : impl_->subscriptions) {
      auto subscription = weak.lock();
      if (!subscription) {
         continue;
      }
      auto subscription_lock = std::scoped_lock{subscription->mutex};
      if (!subscription->active) {
         continue;
      }
      live.push_back(subscription);
      if (!severity_at_least(record.severity, subscription->filter.min_severity) ||
          !topic_matches(record, subscription->filter)) {
         continue;
      }
      if (subscription->queue.size() >= impl_->options.max_subscription_events) {
         ++impl_->metrics.dropped;
         continue;
      }
      subscription->queue.push_back(record);
      ++impl_->metrics.delivered;
   }
   impl_->subscriptions = std::move(live);
}

std::vector<event_record> event_bus::critical_events() const {
   auto lock = std::scoped_lock{impl_->mutex};
   return std::vector<event_record>(impl_->critical.begin(), impl_->critical.end());
}

std::vector<event_record> event_bus::recent_events() const {
   auto lock = std::scoped_lock{impl_->mutex};
   return std::vector<event_record>(impl_->recent.begin(), impl_->recent.end());
}

event_bus_metrics event_bus::metrics() const {
   auto lock = std::scoped_lock{impl_->mutex};
   return impl_->metrics;
}

} // namespace forge::app
