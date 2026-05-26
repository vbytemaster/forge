   boost::asio::awaitable<void> handle_relayed_yamux_stream(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream) {
      auto negotiated = co_await protocol_negotiation::async_accept(std::move(stream), supported_protocols());
      trace_relay(std::string{"relayed yamux: negotiated "} + negotiated.protocol.value);
      if (negotiated.protocol == builtins::ping) {
         co_await handle_ping(std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::identify) {
         co_await handle_identify(std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::identify_push) {
         co_await handle_identify_push(session, std::move(negotiated.stream));
         co_return;
      }
      if (negotiated.protocol == builtins::dcutr) {
         co_await handle_dcutr(session, std::move(negotiated.stream));
         co_return;
      }
      auto handler = handler_for(negotiated.protocol);
      if (!handler) {
         increment_protocol_rejected();
         exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated relayed P2P protocol");
      }
      increment_protocol_accepted();
      co_await (*handler)(node::incoming_protocol_stream{
          .session = session->info,
          .protocol = negotiated.protocol,
          .stream = std::move(negotiated.stream),
      });
   }

   boost::asio::awaitable<void> handle_relay_stop(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto request = relay::codec::decode_stop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      trace_relay("stop: request decoded");
      if (request.kind != relay::stop_message::message_kind::connect || !request.source) {
         co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
             .kind = relay::stop_message::message_kind::status,
             .status = relay::status::malformed_message,
         }));
         co_return;
      }
      co_await stream.async_write(relay::codec::encode_stop(relay::stop_message{
          .kind = relay::stop_message::message_kind::status,
          .limit_value = request.limit_value,
          .status = relay::status::ok,
      }));
      trace_relay("stop: ok sent");

      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      auto yamux = co_await upgrade_relay_inbound_session(std::move(stream), options, request.source->id);
      auto dcutr_started = false;
      while (true) {
         trace_relay("stop: accepting yamux stream");
         auto relayed_stream = co_await yamux->async_accept_stream();
         auto relayed = session->info;
         relayed.remote_peer = request.source->id;
         relayed.path = path::kind::relay;
         relayed.relay_peer = session->info.remote_peer;
         auto relayed_session = std::make_shared<session_state>();
         relayed_session->info = std::move(relayed);
         co_await handle_relayed_yamux_stream(relayed_session, std::move(relayed_stream));
         if (!dcutr_started && options.capabilities.has(capabilities::hole_punching)) {
            dcutr_started = true;
            const auto dcutr_status =
                co_await run_dcutr_initiator(request.source->id, yamux, std::chrono::milliseconds{10'000});
            trace_relay(std::string{"stop: dcutr initiator status="} + std::to_string(static_cast<int>(dcutr_status)));
         }
      }
   }

   boost::asio::awaitable<void> handle_relay_hop(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto relay_buffer = std::vector<std::uint8_t>{};
      auto request = relay::codec::decode_hop(
          co_await async_read_length_delimited(stream, relay_buffer, reachability::options{}.max_message_size));
      trace_relay("hop: request decoded");
      if (request.kind == relay::hop_message::message_kind::reserve) {
         if (!options.relay_policy.service_enabled || !options.capabilities.has(capabilities::relay) ||
             !options.capabilities.has(capabilities::relay_reservation)) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::permission_denied,
            }));
            co_return;
         }
         if (session->info.path == path::kind::relay) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::permission_denied,
            }));
            co_return;
         }
         auto reservation = remember_inbound_relay_reservation(
             session->info.remote_peer, relay::reservation::options{
                                            .ttl = options.limits.relay.reservation_ttl,
                                            .max_streams = options.limits.relay.max_streams_per_reservation,
                                            .max_bytes = options.limits.relay.max_relay_bytes,
                                            .max_queued_bytes = options.limits.relay.max_queued_bytes,
                                        });
         if (!reservation) {
            co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
                .kind = relay::hop_message::message_kind::status,
                .status = relay::status::reservation_refused,
            }));
            co_return;
         }
         auto endpoints = std::vector<endpoint>{};
         if (auto current = local_endpoint_for_control()) {
            endpoints.push_back(p2p_endpoint_for(*current));
         }
         const auto expires_at = std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch() + options.limits.relay.reservation_ttl);
         auto voucher = std::optional<signed_envelope>{};
         if (!options.public_key.empty()) {
            const auto private_key = private_key_from_pem(options.private_key_pem);
            voucher = relay::codec::seal_reservation_voucher(
                relay::voucher{
                    .relay_peer = local,
                    .peer = session->info.remote_peer,
                    .expires_at = static_cast<std::uint64_t>(expires_at.count()),
                },
                decode_public_key(options.public_key), private_key);
         }
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .reservation_value =
                 relay::reservation{
                     .expires_at = static_cast<std::uint64_t>(expires_at.count()),
                     .relay_endpoints = std::move(endpoints),
                     .voucher = std::move(voucher),
                 },
             .limit_value =
                 relay::limit{
                     .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                     .data = options.limits.relay.max_relay_bytes,
                 },
             .status = relay::status::ok,
         }));
         trace_relay("hop: reserve ok sent");
         co_await stream.async_close();
         co_return;
      }

      if (request.kind != relay::hop_message::message_kind::connect || !request.target) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::malformed_message,
         }));
         co_return;
      }
      if (!options.relay_policy.service_enabled) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::permission_denied,
         }));
         co_return;
      }
      const auto relay_owner = request.target->id;
      const auto relay_status = begin_relay(relay_owner);
      trace_relay("hop: connect begin");
      if (relay_status != relay::status::ok) {
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay_status,
         }));
         co_return;
      }

      auto target = std::optional<fcl::p2p::stream>{};
      try {
         auto target_session = co_await ensure_direct_session(request.target->id);
         target.emplace(co_await protocol_negotiation::async_select(
             co_await target_session->connection.async_open_stream(), builtins::relay_stop));
         trace_relay("hop: stop selected");
         co_await target->async_write(relay::codec::encode_stop(relay::stop_message{
             .kind = relay::stop_message::message_kind::connect,
             .source = relay::peer{.id = session->info.remote_peer},
             .limit_value =
                 relay::limit{
                     .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                     .data = options.limits.relay.max_relay_bytes,
                 },
         }));
         auto stop_buffer = std::vector<std::uint8_t>{};
         const auto stop_status = relay::codec::decode_stop(
             co_await async_read_length_delimited(*target, stop_buffer, reachability::options{}.max_message_size));
         trace_relay("hop: stop status decoded");
         if (stop_status.kind != relay::stop_message::message_kind::status || stop_status.status != relay::status::ok) {
            target.reset();
         }
      } catch (...) {
         target.reset();
      }
      if (!target) {
         finish_relay(relay_owner);
         co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
             .kind = relay::hop_message::message_kind::status,
             .status = relay::status::connection_failed,
         }));
         co_return;
      }

      co_await stream.async_write(relay::codec::encode_hop(relay::hop_message{
          .kind = relay::hop_message::message_kind::status,
          .limit_value =
              relay::limit{
                  .duration = std::chrono::duration_cast<std::chrono::seconds>(options.limits.relay.max_duration),
                  .data = options.limits.relay.max_relay_bytes,
              },
          .status = relay::status::ok,
      }));
      trace_relay("hop: connect ok sent, starting pumps");
      stream = detail::stream_access::with_buffer(std::move(stream), std::move(relay_buffer));
      launch_relay_pumps(relay_owner, std::move(stream), std::move(*target));
   }

   void launch_relay_pumps(peer_id owner, fcl::p2p::stream left, fcl::p2p::stream right) {
      auto self = shared_from_this();
      struct relay_pair {
         relay_pair(peer_id owner_value, fcl::p2p::stream left_value, fcl::p2p::stream right_value)
             : owner(std::move(owner_value)), left(std::move(left_value)), right(std::move(right_value)) {}

         peer_id owner;
         fcl::p2p::stream left;
         fcl::p2p::stream right;
         std::mutex mutex;
         std::uint32_t finished = 0;
         std::uint64_t left_to_right_bytes = 0;
         std::uint64_t right_to_left_bytes = 0;
      };
      auto pair = std::make_shared<relay_pair>(std::move(owner), std::move(left), std::move(right));
      auto finish = [self, pair] {
         auto lock = std::scoped_lock{pair->mutex};
         ++pair->finished;
         if (pair->finished == 2) {
            self->finish_relay(pair->owner);
         }
      };
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto chunk = co_await pair->left.async_read();
                   if (chunk.empty()) {
                      trace_relay("pump left->right empty read");
                      break;
                   }
                   trace_relay(std::string{"pump left->right bytes="} + std::to_string(chunk.size()));
                   const auto limit = self->relay_byte_limit(pair->owner);
                   if (pair->left_to_right_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                       pair->left_to_right_bytes + chunk.size() > limit ||
                       !self->add_relay_bytes(pair->owner, chunk.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   pair->left_to_right_bytes += chunk.size();
                   co_await pair->right.async_write(chunk);
                }
             } catch (const fcl::exception::base& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                trace_relay("pump left->right failed");
                self->record_relay_failure();
             }
             try {
                co_await pair->right.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
      asio::co_spawn(
          runtime.context(),
          [self, pair, finish]() -> asio::awaitable<void> {
             try {
                while (true) {
                   auto chunk = co_await pair->right.async_read();
                   if (chunk.empty()) {
                      trace_relay("pump right->left empty read");
                      break;
                   }
                   trace_relay(std::string{"pump right->left bytes="} + std::to_string(chunk.size()));
                   const auto limit = self->relay_byte_limit(pair->owner);
                   if (pair->right_to_left_bytes > limit - std::min<std::uint64_t>(limit, chunk.size()) ||
                       pair->right_to_left_bytes + chunk.size() > limit ||
                       !self->add_relay_bytes(pair->owner, chunk.size())) {
                      self->record_relay_failure();
                      break;
                   }
                   pair->right_to_left_bytes += chunk.size();
                   co_await pair->left.async_write(chunk);
                }
             } catch (const fcl::exception::base& error) {
                if (!is_orderly_stream_close(error)) {
                   self->record_relay_failure();
                }
             } catch (...) {
                trace_relay("pump right->left failed");
                self->record_relay_failure();
             }
             try {
                co_await pair->left.async_close();
             } catch (...) {
                // Relay cleanup is best-effort after either side closes or fails.
             }
             finish();
          },
          asio::detached);
   }
