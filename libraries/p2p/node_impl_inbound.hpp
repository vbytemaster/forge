   void launch_accept_loop() {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self]() -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || !self->listener) {
                      co_return;
                   }
                }
                try {
                   auto connection = co_await self->listener->async_accept();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, connection = std::move(connection)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_inbound_connection(std::move(connection));
                       },
                       asio::detached);
                } catch (const std::exception&) {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped) {
                      co_return;
                   }
                   ++self->metrics_value.handshakes_failed;
                } catch (...) {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped) {
                      co_return;
                   }
                   ++self->metrics_value.handshakes_failed;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_inbound_connection(fcl::quic::connection connection) {
      try {
         const auto remote = verified_peer_id(connection, std::nullopt);
         auto session = std::make_shared<session_state>(session_state{
             .info = node::session_info{.remote_peer = remote,
                                        .capabilities = options.capabilities,
                                        .path = path::kind::direct},
             .connection = std::move(connection),
         });
         remember_session(session);
         launch_session_accept_loop(session);
      } catch (const std::exception&) {
         // The listener owns detached accepts; failed handshakes are reflected in metrics.
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.handshakes_failed;
      } catch (...) {
         // The listener owns detached accepts; failed handshakes are reflected in metrics.
         auto lock = std::scoped_lock{mutex};
         ++metrics_value.handshakes_failed;
      }
      co_return;
   }

   void launch_session_accept_loop(std::shared_ptr<session_state> session) {
      auto self = shared_from_this();
      asio::co_spawn(
          runtime.context(),
          [self, session = std::move(session)]() mutable -> asio::awaitable<void> {
             while (true) {
                {
                   auto lock = std::scoped_lock{self->mutex};
                   if (self->stopped || session->closed) {
                      co_return;
                   }
                }
                try {
                   auto stream = co_await session->connection.async_accept_stream();
                   asio::co_spawn(
                       self->runtime.context(),
                       [self, session, stream = std::move(stream)]() mutable -> asio::awaitable<void> {
                          co_await self->handle_incoming_stream(session, std::move(stream));
                       },
                       asio::detached);
                } catch (...) {
                   session->closed = true;
                   self->forget_session(session->info.remote_peer);
                   co_return;
                }
             }
          },
          asio::detached);
   }

   boost::asio::awaitable<void> handle_incoming_stream(std::shared_ptr<session_state> session, fcl::quic::stream raw) {
      try {
         auto negotiated = co_await protocol_negotiation::async_accept(std::move(raw), supported_protocols());
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
         if (negotiated.protocol == builtins::autonat_v2_dial_request) {
            co_await handle_autonat_v2_dial_request(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::autonat_v2_dial_back) {
            co_await handle_autonat_v2_dial_back(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::autonat_v1) {
            co_await handle_autonat_v1(std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::relay_hop) {
            co_await handle_relay_hop(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::relay_stop) {
            co_await handle_relay_stop(session, std::move(negotiated.stream));
            co_return;
         }
         if (negotiated.protocol == builtins::dcutr) {
            co_await handle_dcutr(session, std::move(negotiated.stream));
            co_return;
         }
         auto handler = handler_for(negotiated.protocol);
         if (!handler) {
            increment_protocol_rejected();
            exceptions::raise(exceptions::code::unsupported_protocol, "unsupported negotiated P2P protocol");
         }
         increment_protocol_accepted();
         co_await (*handler)(node::incoming_protocol_stream{
             .session = session->info,
             .protocol = negotiated.protocol,
             .stream = std::move(negotiated.stream),
         });
      } catch (const std::exception&) {
         increment_protocol_rejected();
      } catch (...) {
         increment_protocol_rejected();
      }
   }

   boost::asio::awaitable<void> handle_ping(fcl::p2p::stream stream) {
      if (!begin_ping_stream()) {
         exceptions::raise(exceptions::code::backpressure_rejected, "libp2p ping inbound stream limit reached");
      }
      try {
         while (true) {
            auto payload = co_await stream.async_read();
            if (payload.size() != 32) {
               exceptions::raise(exceptions::code::protocol_error, "libp2p ping payload must be 32 bytes");
            }
            co_await stream.async_write(payload);
         }
      } catch (const fcl::exception::base& error) {
         finish_ping_stream();
         if (!is_orderly_stream_close(error)) {
            throw;
         }
         co_return;
      } catch (...) {
         finish_ping_stream();
         throw;
      }
      finish_ping_stream();
   }

   boost::asio::awaitable<void> handle_identify(fcl::p2p::stream stream) {
      auto encoded = identify::encode(local_identify_document());
      co_await stream.async_write(wrap_length_delimited(encoded));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_identify_push(std::shared_ptr<session_state> session, fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto payload = unwrap_length_delimited(
          co_await async_read_length_delimited(stream, buffer, options.limits.max_peer_exchange_message_size),
          options.limits.max_peer_exchange_message_size);
      learn_from_identify(session->info.remote_peer, identify::decode(payload));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v2_dial_back(std::shared_ptr<session_state> session,
                                                            fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto request = reachability::codec::decode_v2_dial_back(
          co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
      if (request.nonce == 0 || !consume_autonat_v2_nonce(session->info.remote_peer, request.nonce)) {
         exceptions::raise(exceptions::code::protocol_error, "AutoNAT v2 dial-back nonce mismatch");
      }
      co_await stream.async_write(reachability::codec::encode_v2_dial_back_response(
          reachability::v2::dial_back_response{.status = reachability::v2::dial_back_status::ok}));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v2_dial_request(std::shared_ptr<session_state> session,
                                                               fcl::p2p::stream stream) {
      auto buffer = std::vector<std::uint8_t>{};
      auto request = reachability::codec::decode_v2(
          co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
      auto response = reachability::v2::dial_response{
          .status = reachability::v2::response_status::request_rejected,
          .index = 0,
          .dial_status = reachability::v2::dial_status::unused,
      };
      if (request.type == reachability::v2::message::kind::dial_request && request.dial_request &&
          !request.dial_request->endpoints.empty() && request.dial_request->nonce != 0) {
         response.status = reachability::v2::response_status::dial_refused;
         response.dial_status = reachability::v2::dial_status::dial_error;
         const auto limit = std::min<std::uint64_t>(4096, reachability::options{}.max_data_response_size);
         for (std::size_t index = 0; index < request.dial_request->endpoints.size(); ++index) {
            const auto& candidate = request.dial_request->endpoints[index];
            co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
                .type = reachability::v2::message::kind::dial_data_request,
                .dial_data_request =
                    reachability::v2::dial_data_request{
                        .index = static_cast<std::uint32_t>(index),
                        .bytes = limit,
                    },
            }));
            const auto data = reachability::codec::decode_v2(
                co_await async_read_length_delimited(stream, buffer, reachability::options{}.max_message_size));
            if (data.type != reachability::v2::message::kind::dial_data_response || !data.dial_data_response ||
                data.dial_data_response->data.size() < limit) {
               response.status = reachability::v2::response_status::request_rejected;
               response.dial_status = reachability::v2::dial_status::dial_error;
               break;
            }
            response.index = static_cast<std::uint32_t>(index);
            try {
               auto dialed =
                   co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                          .expected_peer = session->info.remote_peer,
                                                                          .allow_relay = false,
                                                                          .timeout = std::chrono::milliseconds{1'500},
                                                                      });
               try {
                  auto dial_back = co_await protocol_negotiation::async_select(
                      co_await dialed->connection.async_open_stream(), builtins::autonat_v2_dial_back);
                  co_await dial_back.async_write(reachability::codec::encode_v2_dial_back(
                      reachability::v2::dial_back{.nonce = request.dial_request->nonce}));
                  auto dial_back_buffer = std::vector<std::uint8_t>{};
                  const auto dial_back_response =
                      reachability::codec::decode_v2_dial_back_response(co_await async_read_length_delimited(
                          dial_back, dial_back_buffer, reachability::options{}.max_message_size));
                  if (dial_back_response.status == reachability::v2::dial_back_status::ok) {
                     response.status = reachability::v2::response_status::ok;
                     response.dial_status = reachability::v2::dial_status::ok;
                     break;
                  }
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::dial_back_error;
               } catch (...) {
                  response.status = reachability::v2::response_status::ok;
                  response.dial_status = reachability::v2::dial_status::dial_back_error;
               }
            } catch (const fcl::exception::base& error) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = p2p_code(error) == exceptions::code::peer_verification_failed
                                          ? reachability::v2::dial_status::dial_back_error
                                          : reachability::v2::dial_status::dial_error;
            } catch (...) {
               response.status = reachability::v2::response_status::ok;
               response.dial_status = reachability::v2::dial_status::dial_error;
            }
         }
      }
      co_await stream.async_write(reachability::codec::encode_v2(reachability::v2::message{
          .type = reachability::v2::message::kind::dial_response,
          .dial_response = std::move(response),
      }));
      co_await stream.async_close();
   }

   boost::asio::awaitable<void> handle_autonat_v1(fcl::p2p::stream stream) {
      auto request = reachability::codec::decode_v1(co_await stream.async_read());
      auto response = reachability::dial_response{
          .status = reachability::dial_status::bad_request,
          .status_text = "expected AutoNAT dial request",
      };
      if (request.kind == reachability::message::message_kind::dial && request.peer &&
          !request.peer->endpoints.empty()) {
         response.status = reachability::dial_status::dial_error;
         response.status_text = "dial failed";
         for (const auto& candidate : request.peer->endpoints) {
            try {
               auto session =
                   co_await connect_direct(candidate.quic_endpoint(), node::connect_options{
                                                                          .expected_peer = request.peer->peer,
                                                                          .allow_relay = false,
                                                                          .timeout = std::chrono::milliseconds{1'500},
                                                                      });
               session->closed = true;
               forget_session(request.peer->peer);
               try {
                  co_await session->connection.async_close();
               } catch (...) {
                  session->connection.cancel();
               }
               response.status = reachability::dial_status::ok;
               response.status_text.clear();
               response.endpoint = candidate;
               break;
            } catch (const fcl::exception::base& error) {
               response.status = p2p_code(error) == exceptions::code::peer_verification_failed
                                     ? reachability::dial_status::dial_refused
                                     : reachability::dial_status::dial_error;
            } catch (...) {
               response.status = reachability::dial_status::dial_error;
            }
         }
      }
      co_await stream.async_write(reachability::codec::encode_v1(reachability::message{
          .kind = reachability::message::message_kind::dial_response,
          .response = std::move(response),
      }));
      co_await stream.async_close();
   }
