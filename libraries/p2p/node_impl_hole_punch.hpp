   boost::asio::awaitable<void> handle_dcutr(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      trace_relay("dcutr: waiting connect");
      auto buffer = std::vector<std::uint8_t>{};
      auto first = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
      trace_relay(std::string{"dcutr: connect bytes="} + std::to_string(first.size()));
      auto request = hole_punch::codec::decode(first);
      if (request.kind != hole_punch::message::message_kind::connect) {
         exceptions::raise(exceptions::code::protocol_error, "DCUtR expected CONNECT");
      }
      auto observed = std::vector<endpoint>{};
      if (auto endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*endpoint));
      }
      co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
          .kind = hole_punch::message::message_kind::connect,
          .observed_endpoints = std::move(observed),
      }));
      trace_relay("dcutr: connect sent, waiting sync");
      auto sync_bytes = co_await async_read_length_delimited(stream, buffer, hole_punch::options{}.max_message_size);
      trace_relay(std::string{"dcutr: sync bytes="} + std::to_string(sync_bytes.size()));
      auto sync = hole_punch::codec::decode(sync_bytes);
      if (sync.kind != hole_punch::message::message_kind::sync) {
         exceptions::raise(exceptions::code::protocol_error, "DCUtR expected SYNC");
      }
      for (const auto& candidate : request.observed_endpoints) {
         trace_relay(std::string{"dcutr inbound: direct candidate "} + candidate.to_string());
         try {
            (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                         .expected_peer = session->info.remote_peer,
                                                                         .allow_relay = false,
                                                                         .timeout = std::chrono::milliseconds{5'000},
                                                                     });
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return;
         } catch (const std::exception& error) {
            trace_relay(std::string{"dcutr inbound: direct failed "} + error.what());
            record_direct_failure(session->info.remote_peer);
         } catch (...) {
            trace_relay("dcutr inbound: direct failed");
            record_direct_failure(session->info.remote_peer);
         }
      }
      if (co_await wait_for_direct_session(session->info.remote_peer, std::chrono::milliseconds{5'000})) {
         record_hole_punch_result(hole_punch::status::succeeded);
         co_return;
      }
      record_hole_punch_result(hole_punch::status::failed);
   }

   boost::asio::awaitable<bool> wait_for_direct_session(const peer_id& peer, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - started < timeout) {
         if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
            co_return true;
         }
         auto remaining = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started);
         if (remaining <= std::chrono::milliseconds{0}) {
            break;
         }
         auto timer = asio::steady_timer{runtime.context()};
         timer.expires_after(std::min(remaining, std::chrono::milliseconds{50}));
         co_await timer.async_wait(asio::use_awaitable);
      }
      co_return false;
   }

   boost::asio::awaitable<hole_punch::status>
   run_dcutr_initiator(const peer_id& peer, std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
      auto observed = std::vector<endpoint>{};
      if (auto local_endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*local_endpoint));
      }
      if (observed.empty()) {
         record_hole_punch_result(hole_punch::status::failed);
         co_return hole_punch::status::failed;
      }
      try {
         trace_relay("dcutr initiator: open yamux stream");
         auto stream = co_await yamux->async_open_stream();
         stream = co_await protocol_negotiation::async_select(std::move(stream), builtins::dcutr);
         const auto sent = std::chrono::steady_clock::now();
         co_await stream.async_write(hole_punch::codec::encode(hole_punch::message{
             .kind = hole_punch::message::message_kind::connect,
             .observed_endpoints = observed,
         }));
         auto dcutr_buffer = std::vector<std::uint8_t>{};
         auto response = hole_punch::codec::decode(
             co_await async_read_length_delimited(stream, dcutr_buffer, hole_punch::options{}.max_message_size));
         const auto rtt =
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sent);
         trace_relay(std::string{"dcutr initiator: response endpoints="} +
                     std::to_string(response.observed_endpoints.size()));
         if (response.kind != hole_punch::message::message_kind::connect || response.observed_endpoints.empty()) {
            record_hole_punch_result(hole_punch::status::failed);
            co_return hole_punch::status::failed;
         }
         co_await stream.async_write(
             hole_punch::codec::encode(hole_punch::message{.kind = hole_punch::message::message_kind::sync}));
         if (rtt > std::chrono::milliseconds{0}) {
            auto timer = asio::steady_timer{runtime.context()};
            timer.expires_after(rtt / 2);
            co_await timer.async_wait(asio::use_awaitable);
         }
         for (const auto& candidate : response.observed_endpoints) {
            trace_relay(std::string{"dcutr initiator: direct candidate "} + candidate.to_string());
            try {
               (void)co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                            .expected_peer = peer,
                                                                            .allow_relay = false,
                                                                            .timeout = timeout,
                                                                        });
               record_hole_punch_result(hole_punch::status::succeeded);
               co_return hole_punch::status::succeeded;
            } catch (const std::exception& error) {
               trace_relay(std::string{"dcutr initiator: direct failed "} + error.what());
               record_direct_failure(peer);
            } catch (...) {
               trace_relay("dcutr initiator: direct failed");
               record_direct_failure(peer);
            }
         }
         if (co_await wait_for_direct_session(peer, std::min(timeout, std::chrono::milliseconds{5'000}))) {
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return hole_punch::status::succeeded;
         }
      } catch (const std::exception& error) {
         trace_relay(std::string{"dcutr initiator: failed "} + error.what());
      } catch (...) {
         trace_relay("dcutr initiator: failed");
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }

   boost::asio::awaitable<hole_punch::status>
   serve_relayed_streams_until_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                          std::shared_ptr<yamux_session> yamux, std::chrono::milliseconds timeout) {
      const auto started = std::chrono::steady_clock::now();
      for (auto handled = 0U; handled != 8U; ++handled) {
         if (auto existing = session_for(peer); existing && existing->info.path == path::kind::direct) {
            record_hole_punch_result(hole_punch::status::succeeded);
            co_return hole_punch::status::succeeded;
         }
         const auto elapsed =
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
         if (elapsed >= timeout) {
            break;
         }
         auto before = std::uint64_t{0};
         {
            auto lock = std::scoped_lock{mutex};
            before = metrics_value.hole_punch_successes;
         }
         auto relayed_session = std::make_shared<session_state>();
         relayed_session->info = node::session_info{
             .remote_peer = peer,
             .capabilities = capability_set{.bits = capabilities::hole_punching},
             .path = path::kind::relay,
             .relay_peer = relay_peer,
         };
         auto incoming = co_await yamux->async_accept_stream();
         auto negotiated = co_await protocol_negotiation::async_accept(std::move(incoming), supported_protocols());
         trace_relay(std::string{"relayed yamux wait: negotiated "} + negotiated.protocol.value);
         if (negotiated.protocol == builtins::dcutr) {
            co_await handle_dcutr(relayed_session, std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::identify) {
            co_await handle_identify(std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::identify_push) {
            co_await handle_identify_push(relayed_session, std::move(negotiated.stream));
         } else if (negotiated.protocol == builtins::ping) {
            auto self = shared_from_this();
            asio::co_spawn(
                runtime.context(),
                [self, stream = std::move(negotiated.stream)]() mutable -> asio::awaitable<void> {
                   try {
                      co_await self->handle_ping(std::move(stream));
                   } catch (...) {
                      self->increment_protocol_rejected();
                   }
                },
                asio::detached);
         } else {
            auto handler = handler_for(negotiated.protocol);
            if (handler) {
               co_await (*handler)(node::incoming_protocol_stream{
                   .session = relayed_session->info,
                   .protocol = negotiated.protocol,
                   .stream = std::move(negotiated.stream),
               });
            }
         }
         auto after = std::uint64_t{0};
         {
            auto lock = std::scoped_lock{mutex};
            after = metrics_value.hole_punch_successes;
         }
         if (after > before) {
            co_return hole_punch::status::succeeded;
         }
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }

   boost::asio::awaitable<hole_punch::status> attempt_hole_punch(peer_id peer, std::optional<peer_id> relay_peer,
                                                                 std::chrono::milliseconds timeout) {
      validate_operation_timeout(timeout, "P2P hole punch timeout");
      if (session_for(peer)) {
         co_return hole_punch::status::succeeded;
      }
      if (!relay_peer) {
         const auto record = store.find(peer);
         if (record) {
            for (const auto& endpoint : record->endpoints) {
               if (endpoint.relay_peer) {
                  relay_peer = endpoint.relay_peer;
                  break;
               }
            }
         }
      }
      if (!relay_peer) {
         exceptions::raise(exceptions::code::relay_not_available, "P2P hole punching requires a relay peer");
      }
      auto observed = std::vector<endpoint>{};
      if (auto local_endpoint = local_endpoint_for_control()) {
         observed.push_back(p2p_endpoint_for(*local_endpoint));
      }
      if (observed.empty()) {
         record_hole_punch_result(hole_punch::status::failed);
         co_return hole_punch::status::failed;
      }
      try {
         auto yamux = co_await open_relay_yamux(peer, *relay_peer, timeout);
         co_return co_await serve_relayed_streams_until_hole_punch(peer, relay_peer, yamux, timeout);
      } catch (...) {
         // DCUtR failures are expected on many NATs; the caller sees a typed status.
      }
      record_hole_punch_result(hole_punch::status::failed);
      co_return hole_punch::status::failed;
   }
