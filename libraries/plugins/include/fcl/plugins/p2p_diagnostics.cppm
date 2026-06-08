module;

#include <boost/describe.hpp>
#include <fcl/api/api_macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.plugins.p2p_diagnostics;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.exceptions;
import fcl.p2p;
import fcl.plugins.p2p_node;
import fcl.schema;

export namespace fcl::plugins {

class p2p_diagnostics final : public fcl::app::plugin {
 public:
   struct config;
   struct filter;
   class exceptions;
   class api;

   p2p_diagnostics();
   ~p2p_diagnostics() override;

   p2p_diagnostics(const p2p_diagnostics&) = delete;
   p2p_diagnostics& operator=(const p2p_diagnostics&) = delete;

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

class p2p_diagnostics::exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      not_found = 3,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using not_found = fcl::exceptions::coded_exception<code, code::not_found>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(p2p_diagnostics::exceptions::code, "fcl.plugins.p2p_diagnostics")

struct p2p_diagnostics::config {
   std::uint64_t max_peers = 1'024;
   std::uint64_t max_sessions = 1'024;
   std::uint64_t max_endpoints_per_peer = 64;
   std::uint64_t max_protocols_per_peer = 128;
   std::uint64_t max_relay_reservations_per_peer = 64;
};

struct p2p_diagnostics::filter {
   std::optional<fcl::p2p::peer_id> peer;
   bool only_connected = false;
   bool only_protected = false;
   std::uint64_t limit = 0;
};

class p2p_diagnostics::api : public fcl::api::contract<p2p_diagnostics::api> {
 public:
   virtual ~api() = default;

   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot snapshot() const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::snapshot
   snapshot(fcl::p2p::diagnostics::options options) const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::network_state network() const = 0;
   [[nodiscard]] virtual fcl::p2p::resource_manager::snapshot resources() const = 0;
   [[nodiscard]] virtual fcl::p2p::pubsub::snapshot pubsub() const = 0;
   [[nodiscard]] virtual std::vector<fcl::p2p::diagnostics::peer> peers(filter value = {}) const = 0;
   [[nodiscard]] virtual fcl::p2p::diagnostics::peer peer(fcl::p2p::peer_id value) const = 0;

 private:
   friend class p2p_diagnostics;
   class impl;
};

} // namespace fcl::plugins

export {
FCL_API(::fcl::plugins::p2p_diagnostics::api, FCL_API_CONTRACT("fcl.plugins.p2p_diagnostics", 1, 0))
}

BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_diagnostics::config, (),
                      (max_peers, max_sessions, max_endpoints_per_peer, max_protocols_per_peer,
                       max_relay_reservations_per_peer))
BOOST_DESCRIBE_STRUCT(fcl::plugins::p2p_diagnostics::filter, (),
                      (peer, only_connected, only_protected, limit))

export template <> struct fcl::schema::rules<fcl::plugins::p2p_diagnostics::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_diagnostics::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_diagnostics::config>();
      schema.field<&fcl::plugins::p2p_diagnostics::config::max_peers>("max-peers")
         .default_value(std::uint64_t{1'024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_diagnostics::config::max_sessions>("max-sessions")
         .default_value(std::uint64_t{1'024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_diagnostics::config::max_endpoints_per_peer>("max-endpoints-per-peer")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_diagnostics::config::max_protocols_per_peer>("max-protocols-per-peer")
         .default_value(std::uint64_t{128})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_diagnostics::config::max_relay_reservations_per_peer>(
         "max-relay-reservations-per-peer")
         .default_value(std::uint64_t{64})
         .range(1, 1'000'000);
      return schema;
   }
};
