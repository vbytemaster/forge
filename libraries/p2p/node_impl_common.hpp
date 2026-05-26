   [[nodiscard]] std::optional<fcl::quic::endpoint> local_endpoint_for_control() const {
      auto lock = std::scoped_lock{mutex};
      if (listener) {
         return listener->local_endpoint();
      }
      if (!options.advertised_endpoints.empty()) {
         return options.advertised_endpoints.front();
      }
      return std::nullopt;
   }

   [[nodiscard]] fcl::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options.allow_insecure_test_mode) {
         auto security = fcl::quic::security_options{.verify_peer = true};
         security.verifier = [](const fcl::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = fcl::quic::security_options{.verify_peer = true};
      if (expected) {
         security.expected_sha256_fingerprint = expected->value;
      } else {
         security.verifier = [](const fcl::quic::peer_certificate& certificate) {
            return valid_peer_id(make_peer_id_from_certificate(certificate));
         };
      }
      return security;
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected) const {
      return fcl::quic::client_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] fcl::quic::client_options quic_client_options(std::optional<peer_id> expected,
                                                               std::chrono::milliseconds timeout) const {
      auto out = quic_client_options(std::move(expected));
      out.connect_timeout = timeout;
      out.handshake_timeout = timeout;
      return out;
   }

   [[nodiscard]] fcl::quic::server_options quic_server_options() const {
      return fcl::quic::server_options{
          .alpn = "libp2p",
          .limits = options.transport_limits,
          .security = peer_verifier(),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] peer_id verified_peer_id(const fcl::quic::connection& connection,
                                          const std::optional<peer_id>& expected) const {
      if (options.allow_insecure_test_mode) {
         if (expected) {
            return *expected;
         }
         if (const auto certificate = connection.peer_certificate()) {
            return make_peer_id_from_certificate(*certificate);
         }
         return peer_id{.value = "insecure-test-peer"};
      }

      const auto certificate = connection.peer_certificate();
      if (!certificate) {
         exceptions::raise(exceptions::code::peer_verification_failed, "P2P session has no verified peer certificate");
      }
      const auto certificate_peer = make_peer_id_from_certificate(*certificate);
      if (expected && *expected != certificate_peer) {
         exceptions::raise(exceptions::code::peer_verification_failed, "P2P peer id does not match expected peer");
      }
      return certificate_peer;
   }

   void learn_from_message(const peer_exchange_message& message) {
      if (valid_peer_id(message.peer)) {
         store.upsert(peer_store::record{
             .peer = message.peer,
             .capabilities = message.capabilities,
         });
      }
      for (const auto& endpoint : message.endpoints) {
         if (valid_peer_id(endpoint.peer)) {
            store.learn_endpoint(endpoint.peer, endpoint.endpoint, endpoint.capabilities);
         }
      }
   }

   [[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(const fcl::quic::endpoint& value) const {
      return fcl::p2p::endpoint{
          .kind = fcl::p2p::endpoint::address_kind::ip4,
          .host = value.host,
          .port = value.port,
          .peer = local,
      };
   }

   [[nodiscard]] identify::document local_identify_document() const {
      auto endpoints = std::vector<fcl::p2p::endpoint>{};
      endpoints.reserve(options.advertised_endpoints.size() + 1);
      for (const auto& endpoint : options.advertised_endpoints) {
         endpoints.push_back(p2p_endpoint_for(endpoint));
      }
      if (auto endpoint = local_endpoint_for_control()) {
         endpoints.push_back(p2p_endpoint_for(*endpoint));
      }
      return identify::document{
          .protocol_version = options.protocol_version,
          .agent_version = options.agent_version,
          .public_key = options.public_key,
          .listen_endpoints = std::move(endpoints),
          .protocols = supported_protocols(),
      };
   }

   void learn_from_identify(const peer_id& peer, const identify::document& document) {
      auto record = store.find(peer).value_or(peer_store::record{.peer = peer});
      record.protocol_version = document.protocol_version;
      record.agent_version = document.agent_version;
      record.public_key = document.public_key;
      record.protocols = document.protocols;
      record.signed_peer_record = document.signed_peer_record;
      record.observed_endpoint = document.observed_endpoint
                                     ? std::make_optional(document.observed_endpoint->quic_endpoint())
                                     : record.observed_endpoint;
      for (const auto& endpoint : document.listen_endpoints) {
         const auto quic_endpoint = endpoint.quic_endpoint();
         const auto exists = std::ranges::any_of(record.endpoints, [&](const peer_store::endpoint_record& current) {
            return current.endpoint.host == quic_endpoint.host && current.endpoint.port == quic_endpoint.port;
         });
         if (!exists) {
            record.endpoints.push_back(peer_store::endpoint_record{
                .endpoint = quic_endpoint,
                .kind = path::kind::direct,
            });
         }
      }
      store.upsert(std::move(record));
   }

   void remember_session(std::shared_ptr<session_state> session) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.size() >= options.limits.max_sessions && !sessions.contains(session->info.remote_peer)) {
         ++metrics_value.backpressure_rejections;
         exceptions::raise(exceptions::code::backpressure_rejected, "P2P max sessions reached");
      }
      sessions[session->info.remote_peer] = std::move(session);
      metrics_value.active_sessions = sessions.size();
      ++metrics_value.sessions_opened;
      ++metrics_value.handshakes_completed;
   }

   void forget_session(const peer_id& peer) {
      auto lock = std::scoped_lock{mutex};
      if (sessions.erase(peer) != 0) {
         metrics_value.active_sessions = sessions.size();
         ++metrics_value.sessions_closed;
      }
   }

   [[nodiscard]] std::shared_ptr<session_state> session_for(const peer_id& peer) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = sessions.find(peer);
      if (it == sessions.end()) {
         return {};
      }
      return it->second;
   }

   [[nodiscard]] std::optional<node::protocol_handler> handler_for(const protocol_id& protocol) const {
      auto lock = std::scoped_lock{mutex};
      const auto it = handlers.find(protocol);
      if (it == handlers.end()) {
         return std::nullopt;
      }
      return it->second;
   }

   [[nodiscard]] std::vector<protocol_id> supported_protocols() const {
      auto lock = std::scoped_lock{mutex};
      auto out = std::vector<protocol_id>{builtins::ping,
                                          builtins::identify,
                                          builtins::identify_push,
                                          builtins::autonat_v2_dial_request,
                                          builtins::autonat_v2_dial_back,
                                          builtins::autonat_v1,
                                          builtins::relay_stop,
                                          builtins::dcutr};
      if (options.capabilities.has(capabilities::relay) || options.capabilities.has(capabilities::relay_reservation)) {
         out.push_back(builtins::relay_hop);
      }
      out.reserve(out.size() + handlers.size());
      for (const auto& [protocol, _] : handlers) {
         out.push_back(protocol);
      }
      return out;
   }

   void remember_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
      auto lock = std::scoped_lock{mutex};
      pending_autonat_v2_nonces[peer] = nonce;
   }

   void forget_autonat_v2_nonce(const peer_id& peer) {
      auto lock = std::scoped_lock{mutex};
      pending_autonat_v2_nonces.erase(peer);
   }

   [[nodiscard]] bool consume_autonat_v2_nonce(const peer_id& peer, std::uint64_t nonce) {
      auto lock = std::scoped_lock{mutex};
      const auto it = pending_autonat_v2_nonces.find(peer);
      if (it != pending_autonat_v2_nonces.end() && it->second == nonce) {
         pending_autonat_v2_nonces.erase(it);
         return true;
      }
      if (options.allow_insecure_test_mode) {
         const auto nonce_it =
             std::ranges::find_if(pending_autonat_v2_nonces, [&](const auto& item) { return item.second == nonce; });
         if (nonce_it != pending_autonat_v2_nonces.end()) {
            pending_autonat_v2_nonces.erase(nonce_it);
            return true;
         }
      }
      return false;
   }

   void increment_opened_protocol() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_opened;
   }

   void increment_protocol_accepted() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_streams_accepted;
   }

   void increment_protocol_rejected() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.protocol_rejections;
   }

   void increment_peer_exchange() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.peer_exchange_messages;
   }

   [[nodiscard]] bool begin_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams >= 2) {
         ++metrics_value.backpressure_rejections;
         ++metrics_value.protocol_rejections;
         return false;
      }
      ++active_ping_streams;
      return true;
   }

   void finish_ping_stream() {
      auto lock = std::scoped_lock{mutex};
      if (active_ping_streams > 0) {
         --active_ping_streams;
      }
   }

   void increment_reachability_check(reachability::state state) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.reachability_checks;
      if (state == reachability::state::publicly_reachable) {
         ++metrics_value.reachability_public;
      } else if (state == reachability::state::private_network || state == reachability::state::blocked ||
                 state == reachability::state::relay_only) {
         ++metrics_value.reachability_private;
      }
   }

   void cleanup_expired_relay_reservations_locked() {
      const auto now = std::chrono::steady_clock::now();
      for (auto it = inbound_relay_reservations.begin(); it != inbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            if (metrics_value.active_relay_reservations > 0) {
               --metrics_value.active_relay_reservations;
            }
            resources.release_relay_reservation(
                resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
            ++metrics_value.relay_reservation_expirations;
            it = inbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
      for (auto it = outbound_relay_reservations.begin(); it != outbound_relay_reservations.end();) {
         if (it->second.canceled || it->second.expires_at <= now) {
            it = outbound_relay_reservations.erase(it);
         } else {
            ++it;
         }
      }
   }

   [[nodiscard]] bool has_outbound_relay_reservation(const peer_id& relay_peer) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      return outbound_relay_reservations.contains(relay_peer);
   }

   bool remember_outbound_relay_reservation(relay_reservation_state reservation) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      outbound_relay_reservations[reservation.relay_peer] = std::move(reservation);
      return true;
   }

   void remember_relay_reservation_in_store(const relay::reservation::info& info) {
      auto record = store.find(info.relay_peer).value_or(peer_store::record{.peer = info.relay_peer});
      auto relay_endpoints = std::vector<fcl::quic::endpoint>{};
      relay_endpoints.reserve(info.relay_endpoints.size());
      for (const auto& endpoint : info.relay_endpoints) {
         relay_endpoints.push_back(endpoint.quic_endpoint());
      }
      auto reservation = peer_store::relay_record{
          .relay = info.relay_peer,
          .reservation_id = info.id,
          .expires_at = std::chrono::system_clock::time_point{info.expires_at},
          .endpoints = std::move(relay_endpoints),
          .voucher = info.voucher ? info.voucher->encode() : std::vector<std::uint8_t>{},
      };
      const auto current = std::ranges::find_if(record.relay_reservations,
                                                [&](const auto& value) { return value.relay == info.relay_peer; });
      if (current == record.relay_reservations.end()) {
         record.relay_reservations.push_back(std::move(reservation));
      } else {
         *current = std::move(reservation);
      }
      record.capabilities.add(capabilities::relay);
      record.capabilities.add(capabilities::relay_reservation);
      store.upsert(std::move(record));
   }

   [[nodiscard]] std::optional<relay_reservation_state>
   remember_inbound_relay_reservation(const peer_id& owner, relay::reservation::options request) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (inbound_relay_reservations.size() >= options.limits.relay.max_reservations &&
          !inbound_relay_reservations.contains(owner)) {
         ++metrics_value.relay_reservation_rejections;
         return std::nullopt;
      }
      if (!inbound_relay_reservations.contains(owner) &&
          !resources.try_acquire_relay_reservation(
              resource_manager::scope{.peer = owner, .protocol = builtins::relay_hop})) {
         ++metrics_value.relay_reservation_rejections;
         return std::nullopt;
      }
      const auto ttl = std::min(request.ttl, options.limits.relay.reservation_ttl);
      auto reservation = relay_reservation_state{
          .owner = owner,
          .relay_peer = local,
          .id = next_reservation_id++,
          .expires_at = std::chrono::steady_clock::now() + ttl,
          .max_streams = std::min(request.max_streams, options.limits.relay.max_streams_per_reservation),
          .max_bytes = std::min(request.max_bytes, options.limits.relay.max_relay_bytes),
          .max_queued_bytes = std::min(request.max_queued_bytes, options.limits.relay.max_queued_bytes),
      };
      inbound_relay_reservations[owner] = reservation;
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      ++metrics_value.relay_reservations;
      return reservation;
   }

   bool cancel_inbound_relay_reservation(const peer_id& owner, std::uint64_t reservation_id) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      const auto it = inbound_relay_reservations.find(owner);
      if (it == inbound_relay_reservations.end() || (reservation_id != 0 && it->second.id != reservation_id)) {
         return false;
      }
      resources.release_relay_reservation(
          resource_manager::scope{.peer = it->second.owner, .protocol = builtins::relay_hop});
      inbound_relay_reservations.erase(it);
      metrics_value.active_relay_reservations = inbound_relay_reservations.size();
      return true;
   }

   relay::status begin_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      if (metrics_value.active_relays >= options.limits.relay.max_active_relays ||
          !resources.try_acquire_relay_stream()) {
         ++metrics_value.relay_rejections;
         return relay::status::resource_limit_exceeded;
      }
      if (options.limits.relay.require_reservation) {
         const auto reservation = inbound_relay_reservations.find(owner);
         if (reservation == inbound_relay_reservations.end()) {
            resources.release_relay_stream();
            ++metrics_value.relay_rejections;
            return relay::status::no_reservation;
         }
         if (reservation->second.active_streams >= reservation->second.max_streams ||
             reservation->second.bytes >= reservation->second.max_bytes) {
            resources.release_relay_stream();
            ++metrics_value.relay_rejections;
            return relay::status::resource_limit_exceeded;
         }
         ++reservation->second.active_streams;
      }
      ++metrics_value.active_relays;
      ++metrics_value.relays_opened;
      return relay::status::ok;
   }

   [[nodiscard]] std::uint64_t relay_byte_limit(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      cleanup_expired_relay_reservations_locked();
      const auto reservation = inbound_relay_reservations.find(owner);
      if (reservation != inbound_relay_reservations.end()) {
         return reservation->second.max_bytes;
      }
      return options.limits.relay.max_relay_bytes;
   }

   void finish_relay(const peer_id& owner) {
      auto lock = std::scoped_lock{mutex};
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation != inbound_relay_reservations.end() && reservation->second.active_streams > 0) {
         --reservation->second.active_streams;
      }
      if (metrics_value.active_relays > 0) {
         --metrics_value.active_relays;
      }
      resources.release_relay_stream();
   }

   bool add_relay_bytes(const peer_id& owner, std::uint64_t bytes) {
      auto lock = std::scoped_lock{mutex};
      if (!resources.add_relay_bytes(bytes)) {
         ++metrics_value.relay_rejections;
         return false;
      }
      metrics_value.relay_bytes += bytes;
      auto reservation = inbound_relay_reservations.find(owner);
      if (reservation == inbound_relay_reservations.end()) {
         return !options.limits.relay.require_reservation;
      }
      if (reservation->second.bytes + bytes > reservation->second.max_bytes) {
         ++metrics_value.relay_rejections;
         return false;
      }
      reservation->second.bytes += bytes;
      return true;
   }

   void record_path_open(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_opens;
      } else {
         ++metrics_value.path_relay_opens;
      }
   }

   void record_path_attempt(path::kind kind) {
      auto lock = std::scoped_lock{mutex};
      if (kind == path::kind::direct) {
         ++metrics_value.path_direct_attempts;
      } else {
         ++metrics_value.path_relay_attempts;
      }
   }

   void record_hole_punch_result(hole_punch::status status) {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.hole_punch_attempts;
      if (status == hole_punch::status::succeeded) {
         ++metrics_value.hole_punch_successes;
      } else if (status == hole_punch::status::failed) {
         ++metrics_value.hole_punch_failures;
      }
   }

   void record_direct_failure(const peer_id& peer) {
      store.mark_failure(peer);
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.direct_failures;
   }

   void record_relay_failure() {
      auto lock = std::scoped_lock{mutex};
      ++metrics_value.relay_failures;
   }
