   boost::asio::awaitable<std::shared_ptr<session_state>> connect_direct(fcl::quic::endpoint endpoint,
                                                                         node::connect_options connect_options_value) {
      validate_operation_timeout(connect_options_value.timeout, "P2P connect timeout");
      auto deadline = std::unique_ptr<operation_deadline>{};
      auto endpoint_copy = endpoint;
      try {
         auto started = std::chrono::steady_clock::now();
         auto connection = std::make_shared<fcl::quic::connection>(co_await connector.async_connect(
             std::move(endpoint),
             quic_client_options(connect_options_value.expected_peer, connect_options_value.timeout)));
         deadline = std::make_unique<operation_deadline>(
             runtime.context(), remaining_timeout(started, connect_options_value.timeout, "P2P connect"));
         deadline->arm([connection] { connection->cancel(); });
         if (!deadline->finish()) {
            throw_operation_timeout("P2P connect");
         }
         const auto remote = verified_peer_id(*connection, connect_options_value.expected_peer);
         store.mark_endpoint_success(
             remote, endpoint_copy, path::kind::direct,
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started));
         auto session = std::make_shared<session_state>(session_state{
             .info = node::session_info{.remote_peer = remote,
                                        .capabilities = options.capabilities,
                                        .path = path::kind::direct},
             .connection = std::move(*connection),
             .direct_endpoint = endpoint_copy,
         });
         remember_session(session);
         launch_session_accept_loop(session);
         co_return session;
      } catch (const fcl::exception::base& error) {
         if (deadline && deadline->timed_out()) {
            throw_operation_timeout("P2P connect");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<std::shared_ptr<session_state>> ensure_direct_session(
       const peer_id& peer, std::chrono::milliseconds timeout = node::connect_options{}.timeout,
       std::size_t max_direct_endpoints = node::connect_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::connect_options{}.direct_attempt_timeout) {
      if (auto existing = session_for(peer)) {
         co_return existing;
      }
      const auto record = store.find(peer);
      if (!record || record->endpoints.empty()) {
         exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no known direct endpoint");
      }
      if (max_direct_endpoints == 0) {
         exceptions::raise(exceptions::code::invalid_options, "P2P max direct endpoints must be positive");
      }
      auto endpoints = record->endpoints;
      const auto now = std::chrono::system_clock::now();
      auto preferred = std::vector<peer_store::endpoint_record>{};
      for (const auto& endpoint : endpoints) {
         if (endpoint.kind != path::kind::direct || endpoint.relay_peer) {
            continue;
         }
         if (endpoint.backoff_until != std::chrono::system_clock::time_point{} && endpoint.backoff_until > now) {
            continue;
         }
         preferred.push_back(endpoint);
      }
      if (preferred.empty()) {
         for (const auto& endpoint : endpoints) {
            if (endpoint.kind == path::kind::direct && !endpoint.relay_peer) {
               preferred.push_back(endpoint);
            }
         }
      }
      std::stable_sort(preferred.begin(), preferred.end(),
                       [](const auto& left, const auto& right) { return left.score > right.score; });

      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<exceptions::code>{};
      auto last_message = std::string{};
      const auto attempts = std::min(max_direct_endpoints, preferred.size());
      for (std::size_t index = 0; index < attempts; ++index) {
         const auto remaining = remaining_timeout(started, timeout, "P2P direct path");
         const auto per_attempt = attempt_timeout(remaining, direct_attempt_timeout, "P2P direct path attempt");
         const auto endpoint = preferred[index].endpoint;
         record_path_attempt(path::kind::direct);
         try {
            co_return co_await connect_direct(
                endpoint, node::connect_options{.expected_peer = peer, .allow_relay = false, .timeout = per_attempt});
         } catch (const fcl::exception::base& error) {
            last_kind = p2p_code(error);
            last_message = error.what();
            store.mark_endpoint_failure(peer, endpoint, path::kind::direct,
                                        std::chrono::system_clock::now() + std::chrono::seconds{5});
            record_direct_failure(peer);
         }
      }
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::peer_not_found, "P2P peer has no direct endpoint outside backoff");
   }

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_direct(
       const peer_id& peer, const protocol_id& protocol, std::chrono::milliseconds timeout,
       std::size_t max_direct_endpoints = node::open_options{}.max_direct_endpoints,
       std::chrono::milliseconds direct_attempt_timeout = node::open_options{}.direct_attempt_timeout) {
      const auto started = std::chrono::steady_clock::now();
      auto last_kind = std::optional<exceptions::code>{};
      auto last_message = std::string{};
      for (std::size_t attempt = 0; attempt < max_direct_endpoints; ++attempt) {
         const auto remaining = remaining_timeout(started, timeout, "P2P protocol open");
         auto session = co_await ensure_direct_session(peer, remaining, max_direct_endpoints, direct_attempt_timeout);
         auto deadline = operation_deadline{
             runtime.context(), attempt_timeout(remaining, direct_attempt_timeout, "P2P protocol open direct attempt")};
         deadline.arm([session] { session->connection.cancel(); });
         record_path_attempt(path::kind::direct);
         try {
            auto selected =
                co_await protocol_negotiation::async_select(co_await session->connection.async_open_stream(), protocol);
            if (!deadline.finish()) {
               throw_operation_timeout("P2P protocol open");
            }
            increment_opened_protocol();
            record_path_open(path::kind::direct);
            co_return selected;
         } catch (const fcl::exception::base& error) {
            if (!deadline.finish() || deadline.timed_out()) {
               session->closed = true;
               forget_session(peer);
               if (session->direct_endpoint) {
                  store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                              std::chrono::system_clock::now() + std::chrono::seconds{5});
               }
               record_direct_failure(peer);
               last_kind = exceptions::code::timeout;
               last_message = "P2P protocol open timed out";
               continue;
            }
            const auto p2p_kind = exceptions::code_of(error);
            if (p2p_kind == exceptions::code::unsupported_protocol || p2p_kind == exceptions::code::protocol_error ||
                p2p_kind == exceptions::code::codec_error) {
               throw;
            }
            session->closed = true;
            forget_session(peer);
            if (session->direct_endpoint) {
               store.mark_endpoint_failure(peer, *session->direct_endpoint, path::kind::direct,
                                           std::chrono::system_clock::now() + std::chrono::seconds{5});
            }
            record_direct_failure(peer);
            last_kind = p2p_kind ? *p2p_kind : map_quic_error(quic_code(error));
            last_message = error.what();
            continue;
         }
      }
      if (last_kind) {
         exceptions::raise(*last_kind, last_message);
      }
      exceptions::raise(exceptions::code::peer_not_found, "P2P direct path attempts were exhausted");
   }

   boost::asio::awaitable<relay::reservation::info>
   request_relay_reservation(const peer_id& relay_peer, relay::reservation::options reservation_options,
                             std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P relay reservation timeout");
      if (!options.relay_policy.client_enabled) {
         exceptions::raise(exceptions::code::relay_not_available, "P2P relay client policy is disabled");
      }
      if (reservation_options.ttl.count() <= 0 || reservation_options.max_streams == 0 ||
          reservation_options.max_bytes == 0 || reservation_options.max_queued_bytes == 0) {
         exceptions::raise(exceptions::code::invalid_options, "invalid P2P relay reservation options");
      }
      const auto started = std::chrono::steady_clock::now();
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay reservation")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto stream = co_await protocol_negotiation::async_select(
             co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
         co_await stream.async_write(
             relay::codec::encode_hop(relay::hop_message{.kind = relay::hop_message::message_kind::reserve}));
         auto relay_buffer = std::vector<std::uint8_t>{};
         auto response = relay::codec::decode_hop(
             co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay reservation");
         }
         if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok ||
             !response.reservation_value) {
            exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                      : exceptions::code::protocol_error,
                            "P2P relay reservation rejected");
         }
         const auto now_seconds =
             std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
         const auto expires_at =
             std::chrono::seconds{static_cast<std::int64_t>(response.reservation_value->expires_at)};
         const auto ttl = expires_at > now_seconds ? expires_at - now_seconds : std::chrono::seconds{1};
         const auto limit = response.limit_value.value_or(relay::limit{
             .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
             .data = options.limits.relay.max_relay_bytes,
         });
         auto info = relay::reservation::info{
             .relay_peer = relay_peer,
             .id = response.reservation_value->expires_at,
             .expires_at = expires_at,
             .ttl = std::chrono::duration_cast<std::chrono::milliseconds>(ttl),
             .max_streams = reservation_options.max_streams,
             .max_bytes = limit.data == 0 ? reservation_options.max_bytes : limit.data,
             .max_queued_bytes = reservation_options.max_queued_bytes,
             .relay_endpoints = response.reservation_value->relay_endpoints,
             .voucher = response.reservation_value->voucher,
         };
         // libp2p Circuit Relay v2 vouchers are signed envelopes. Keep the
         // envelope bytes intact here; validation belongs to the signed-envelope
         // layer, not to the older FCL-local voucher shape.
         remember_outbound_relay_reservation(relay_reservation_state{
             .owner = local,
             .relay_peer = relay_peer,
             .id = info.id,
             .expires_at = std::chrono::steady_clock::now() + info.ttl,
             .max_streams = info.max_streams,
             .max_bytes = info.max_bytes,
             .max_queued_bytes = info.max_queued_bytes,
         });
         remember_relay_reservation_in_store(info);
         co_return info;
      } catch (const fcl::exception::base& error) {
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay reservation");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<void> ensure_relay_reservation(const peer_id& relay_peer, std::chrono::milliseconds timeout) {
      if (has_outbound_relay_reservation(relay_peer)) {
         co_return;
      }
      (void)co_await request_relay_reservation(relay_peer,
                                               relay::reservation::options{
                                                   .ttl = options.limits.relay.reservation_ttl,
                                                   .max_streams = options.limits.relay.max_streams_per_reservation,
                                                   .max_bytes = options.limits.relay.max_relay_bytes,
                                                   .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                               },
                                               timeout);
   }

   boost::asio::awaitable<std::shared_ptr<yamux_session>>
   open_relay_yamux(const peer_id& peer, const peer_id& relay_peer, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      record_path_attempt(path::kind::relay);
      auto relay_session = co_await ensure_direct_session(relay_peer, timeout);
      auto deadline =
          operation_deadline{runtime.context(), remaining_timeout(started, timeout, "P2P relay protocol open")};
      deadline.arm([relay_session] { relay_session->connection.cancel(); });
      try {
         auto stream = co_await protocol_negotiation::async_select(
             co_await relay_session->connection.async_open_stream(), builtins::relay_hop);
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::connect,
             .target = relay::peer{.id = peer},
         }));
         auto relay_buffer = std::vector<std::uint8_t>{};
         auto response = relay::codec::decode_hop(
             co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
         if (!deadline.finish()) {
            throw_operation_timeout("P2P relay protocol open");
         }
         if (response.kind != relay::hop_message::message_kind::status || response.status != relay::status::ok) {
            exceptions::raise(response.kind == relay::hop_message::message_kind::status ? exceptions::code::relay_rejected
                                                                                      : exceptions::code::protocol_error,
                            response.kind == relay::hop_message::message_kind::status
                                ? "P2P relay open rejected with status " +
                                      std::to_string(static_cast<std::uint16_t>(response.status))
                                : "P2P relay open rejected with unexpected response");
         }
         record_path_open(path::kind::relay);
         stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
         co_return co_await upgrade_relay_outbound_session(std::move(stream), options, peer);
      } catch (const fcl::exception::base& error) {
         record_relay_failure();
         if (deadline.timed_out()) {
            relay_session->closed = true;
            forget_session(relay_peer);
            throw_operation_timeout("P2P relay protocol open");
         }
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<fcl::p2p::stream> open_protocol_via_relay(const peer_id& peer, const protocol_id& protocol,
                                                                    const peer_id& relay_peer,
                                                                    std::chrono::milliseconds timeout) {
      auto yamux = co_await open_relay_yamux(peer, relay_peer, timeout);
      trace_relay("outbound upgrade: open yamux stream");
      auto substream = co_await yamux->async_open_stream();
      auto selected = co_await protocol_negotiation::async_select(std::move(substream), protocol);
      co_return selected;
   }

   boost::asio::awaitable<void> request_peer_exchange(const peer_id& peer) {
      auto session = co_await ensure_direct_session(peer);
      try {
         auto framed = fcl::quic::framed_stream{
             co_await session->connection.async_open_stream(),
             frame_codec_for(options),
         };
         co_await peer_exchange_codec::async_write(framed,
                                                   peer_exchange_message{
                                                       .kind = peer_exchange_message::type::peer_exchange_request,
                                                       .peer = local,
                                                   },
                                                   codec_for(options));
         auto response = co_await peer_exchange_codec::async_read(framed, codec_for(options));
         if (response.kind != peer_exchange_message::type::peer_exchange_response) {
            exceptions::raise(exceptions::code::protocol_error, "P2P peer exchange expected response");
         }
         learn_from_message(response);
         increment_peer_exchange();
      } catch (const fcl::exception::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }
