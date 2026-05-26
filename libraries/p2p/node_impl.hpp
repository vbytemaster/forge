#pragma once

#include "node_helpers.hpp"
#include "relay_transport.hpp"
#include "operation_deadline.hpp"

namespace fcl::p2p {

struct node::impl : std::enable_shared_from_this<impl> {
   struct session_state {
      node::session_info info;
      fcl::quic::connection connection;
      std::optional<fcl::quic::endpoint> direct_endpoint;
      bool closed = false;
   };

   struct relay_reservation_state {
      peer_id owner;
      peer_id relay_peer;
      std::uint64_t id = 0;
      std::chrono::steady_clock::time_point expires_at{};
      std::size_t max_streams = 0;
      std::uint64_t max_bytes = 0;
      std::size_t max_queued_bytes = 0;
      std::size_t active_streams = 0;
      std::uint64_t bytes = 0;
      bool canceled = false;
   };

   impl(fcl::asio::runtime& runtime_value, node::options options_value)
       : runtime(runtime_value), options(std::move(options_value)),
         local(options.explicit_peer_id ? *options.explicit_peer_id
                                        : make_peer_id_from_certificate_pem(options.certificate_pem)),
         connector(runtime_value), store(peer_store::options{.backend = make_peer_store_backend(options)}) {}

   fcl::asio::runtime& runtime;
   node::options options;
   peer_id local;
   fcl::quic::connector connector;
   std::unique_ptr<fcl::quic::listener> listener;

   mutable std::mutex mutex;
   peer_store store;
   std::map<protocol_id, node::protocol_handler> handlers;
   std::map<peer_id, std::shared_ptr<session_state>> sessions;
   std::map<peer_id, relay_reservation_state> inbound_relay_reservations;
   std::map<peer_id, relay_reservation_state> outbound_relay_reservations;
   std::map<peer_id, std::uint64_t> pending_autonat_v2_nonces;
   std::uint64_t next_reservation_id = 1;
   resource_manager resources{options.limits.resources};
   node::metrics_snapshot metrics_value;
   std::size_t active_ping_streams = 0;
   bool stopped = false;


#include "node_impl_common.hpp"
#include "node_impl_paths.hpp"
#include "node_impl_inbound.hpp"
#include "node_impl_relay.hpp"
#include "node_impl_hole_punch.hpp"
#include "node_impl_maintenance.hpp"
};

} // namespace fcl::p2p
