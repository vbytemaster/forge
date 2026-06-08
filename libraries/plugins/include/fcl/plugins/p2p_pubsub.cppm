module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <fcl/api/api_macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.plugins.p2p_pubsub;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.exceptions;
import fcl.p2p;
import fcl.raw.raw;
import fcl.schema;

export namespace fcl::plugins {

class p2p_pubsub final : public fcl::app::plugin {
 public:
   struct config;
   struct publish_options;
   struct subscribe_options;
   struct message;
   template <typename T> struct typed_message;
   struct subscription;
   struct snapshot;
   class exceptions;
   class api;

   using handler = std::function<boost::asio::awaitable<fcl::p2p::pubsub::validation_result>(message)>;
   template <typename T>
   using typed_handler =
      std::function<boost::asio::awaitable<fcl::p2p::pubsub::validation_result>(typed_message<T>)>;

   p2p_pubsub();
   ~p2p_pubsub() override;

   p2p_pubsub(const p2p_pubsub&) = delete;
   p2p_pubsub& operator=(const p2p_pubsub&) = delete;

   [[nodiscard]] static fcl::app::plugin_descriptor descriptor();

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

class p2p_pubsub::exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      topic_not_allowed = 3,
      subscription_not_found = 4,
      handler_limit = 5,
      message_too_large = 6,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using topic_not_allowed = fcl::exceptions::coded_exception<code, code::topic_not_allowed>;
   using subscription_not_found = fcl::exceptions::coded_exception<code, code::subscription_not_found>;
   using handler_limit = fcl::exceptions::coded_exception<code, code::handler_limit>;
   using message_too_large = fcl::exceptions::coded_exception<code, code::message_too_large>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(p2p_pubsub::exceptions::code, "fcl.plugins.p2p_pubsub")

struct p2p_pubsub::config {
   std::uint64_t max_topics = 1'024;
   std::uint64_t max_handlers_per_topic = 64;
   std::uint64_t max_active_handlers = 4'096;
   std::uint64_t max_message_size = 1024 * 1024;
   std::uint64_t handler_deadline_ms = 5'000;
   std::vector<std::string> allowed_topics;
   std::vector<std::string> denied_topics;
   bool sign_publishes = true;
};

struct p2p_pubsub::publish_options {
   std::optional<bool> sign;
};

struct p2p_pubsub::subscribe_options {
   std::chrono::milliseconds handler_deadline{0};
};

struct p2p_pubsub::message {
   fcl::p2p::peer_id source;
   std::optional<fcl::p2p::peer_id> author;
   fcl::p2p::pubsub::topic subject;
   std::vector<std::uint8_t> data;
   std::vector<std::uint8_t> seqno;
};

template <typename T> struct p2p_pubsub::typed_message {
   fcl::p2p::peer_id source;
   std::optional<fcl::p2p::peer_id> author;
   fcl::p2p::pubsub::topic subject;
   T value;
   std::vector<std::uint8_t> seqno;
};

struct p2p_pubsub::subscription {
   std::uint64_t id = 0;
   fcl::p2p::pubsub::topic subject;
};

struct p2p_pubsub::snapshot {
   std::size_t topics = 0;
   std::size_t subscriptions = 0;
   std::size_t active_handlers = 0;
   std::uint64_t messages_published = 0;
   std::uint64_t messages_delivered = 0;
   std::uint64_t messages_accepted = 0;
   std::uint64_t messages_rejected = 0;
   std::uint64_t messages_ignored = 0;
   std::uint64_t messages_dropped = 0;
   std::uint64_t handler_failures = 0;
   fcl::p2p::pubsub::snapshot core;
};

class p2p_pubsub::api : public fcl::api::contract<p2p_pubsub::api> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                                                   publish_options options = {}) = 0;
   virtual boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, handler callback,
                                                          subscribe_options options = {}) = 0;
   virtual boost::asio::awaitable<void> unsubscribe(subscription value) = 0;
   [[nodiscard]] virtual std::vector<subscription> subscriptions() const = 0;
   [[nodiscard]] virtual snapshot snapshot() const = 0;

   template <typename T>
      requires(!std::same_as<std::remove_cvref_t<T>, std::vector<std::uint8_t>> &&
               !std::same_as<std::remove_cvref_t<T>, std::vector<char>>)
   boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, const T& value,
                                           publish_options options = {}) {
      auto packed = fcl::raw::pack(value);
      auto bytes = std::vector<std::uint8_t>{packed.begin(), packed.end()};
      co_return co_await publish(std::move(subject), std::move(bytes), options);
   }

   template <typename T>
   boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, typed_handler<T> callback,
                                                  subscribe_options options = {}) {
      auto wrapper = [callback = std::move(callback)](message value) mutable
         -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
         auto typed = typed_message<T>{
            .source = std::move(value.source),
            .author = std::move(value.author),
            .subject = std::move(value.subject),
            .value = fcl::raw::unpack<T>(value.data),
            .seqno = std::move(value.seqno),
         };
         co_return co_await callback(std::move(typed));
      };
      co_return co_await subscribe(std::move(subject), std::move(wrapper), options);
   }

 private:
   friend class p2p_pubsub;
   class impl;
};

} // namespace fcl::plugins

export {
FCL_API(::fcl::plugins::p2p_pubsub::api, FCL_API_CONTRACT("fcl.plugins.p2p_pubsub", 1, 0))
}

BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::config, (),
                      (max_topics, max_handlers_per_topic, max_active_handlers, max_message_size, handler_deadline_ms,
                       allowed_topics, denied_topics, sign_publishes))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::publish_options, (), (sign))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::subscribe_options, (), (handler_deadline))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::message, (), (source, author, subject, data, seqno))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::subscription, (), (id, subject))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_pubsub::snapshot, (),
                      (topics, subscriptions, active_handlers, messages_published, messages_delivered,
                       messages_accepted, messages_rejected, messages_ignored, messages_dropped, handler_failures,
                       core))

export template <> struct fcl::schema::rules<fcl::plugins::p2p_pubsub::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_pubsub::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_pubsub::config>();
      schema.field<&fcl::plugins::p2p_pubsub::config::max_topics>("max-topics")
         .default_value(std::uint64_t{1'024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_pubsub::config::max_handlers_per_topic>("max-handlers-per-topic")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_pubsub::config::max_active_handlers>("max-active-handlers")
         .default_value(std::uint64_t{4'096})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_pubsub::config::max_message_size>("max-message-size")
         .default_value(std::uint64_t{1024 * 1024})
         .range(1, 1024 * 1024 * 1024);
      schema.field<&fcl::plugins::p2p_pubsub::config::handler_deadline_ms>("handler-deadline-ms")
         .default_value(std::uint64_t{5'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_pubsub::config::allowed_topics>("allowed-topics")
         .default_value(std::vector<std::string>{});
      schema.field<&fcl::plugins::p2p_pubsub::config::denied_topics>("denied-topics")
         .default_value(std::vector<std::string>{});
      schema.field<&fcl::plugins::p2p_pubsub::config::sign_publishes>("sign-publishes").default_value(true);
      return schema;
   }
};
