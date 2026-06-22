module;

#include <boost/describe.hpp>

#include <cstdint>
#include <memory>
#include <new>
#include <optional>

export module fcl.plugins.p2p_diagnostics.types;

import fcl.p2p.identity;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::plugins::p2p_diagnostics {

struct config {
   std::uint64_t max_peers = 1'024;
   std::uint64_t max_sessions = 1'024;
   std::uint64_t max_endpoints_per_peer = 64;
   std::uint64_t max_protocols_per_peer = 128;
   std::uint64_t max_relay_reservations_per_peer = 64;
};

struct filter {
   std::optional<fcl::p2p::peer_id> peer;
   bool only_connected = false;
   bool only_protected = false;
   std::uint64_t limit = 0;
};

BOOST_DESCRIBE_STRUCT(config, (),
                      (max_peers, max_sessions, max_endpoints_per_peer, max_protocols_per_peer,
                       max_relay_reservations_per_peer))
BOOST_DESCRIBE_STRUCT(filter, (), (peer, only_connected, only_protected, limit))

} // namespace fcl::plugins::p2p_diagnostics

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
