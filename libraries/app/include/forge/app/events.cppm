module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module forge.app.events;

export namespace forge::app {

enum class event_severity : std::uint8_t {
   debug,
   info,
   warning,
   error,
   critical,
};

struct event_record {
   std::uint64_t id = 0;
   std::chrono::system_clock::time_point time;
   event_severity severity = event_severity::info;
   std::string topic;
   std::string message;
};

struct event_filter {
   std::string topic;
   event_severity min_severity = event_severity::debug;
   bool include_child_topics = true;
};

struct event_bus_options {
   std::uint64_t max_subscription_events = 64;
   std::uint64_t max_critical_events = 128;
   std::uint64_t max_recent_events = 128;
};

struct event_bus_metrics {
   std::uint64_t published = 0;
   std::uint64_t delivered = 0;
   std::uint64_t dropped = 0;
   std::uint64_t subscriptions = 0;
};

class event_subscription {
 public:
   event_subscription();
   ~event_subscription();

   event_subscription(event_subscription&&) noexcept;
   event_subscription& operator=(event_subscription&&) noexcept;

   event_subscription(const event_subscription&) = delete;
   event_subscription& operator=(const event_subscription&) = delete;

   [[nodiscard]] std::optional<event_record> poll();
   void unsubscribe();

 private:
   struct state;
   std::shared_ptr<state> state_;

   explicit event_subscription(std::shared_ptr<state> state);

   friend class event_bus;
};

class event_bus {
 public:
   explicit event_bus(event_bus_options options = {});
   ~event_bus();

   event_bus(const event_bus&) = delete;
   event_bus& operator=(const event_bus&) = delete;

   [[nodiscard]] event_subscription subscribe(event_filter filter = {});
   void publish(event_severity severity, std::string topic, std::string message);
   [[nodiscard]] std::vector<event_record> critical_events() const;
   [[nodiscard]] std::vector<event_record> recent_events() const;
   [[nodiscard]] event_bus_metrics metrics() const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::app
