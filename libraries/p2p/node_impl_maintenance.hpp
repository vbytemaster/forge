   boost::asio::awaitable<void> handle_peer_exchange(fcl::quic::framed_stream framed, std::uint64_t request_id) {
      auto endpoints = std::vector<peer_exchange_message::endpoint_record>{};
      const auto snapshot = store.snapshot();
      for (const auto& record : snapshot) {
         for (const auto& endpoint : record.endpoints) {
            endpoints.push_back(peer_exchange_message::endpoint_record{
                .peer = record.peer,
                .endpoint = endpoint.endpoint,
                .capabilities = record.capabilities,
            });
            if (endpoints.size() >= options.limits.max_peer_exchange_records) {
               break;
            }
         }
         if (endpoints.size() >= options.limits.max_peer_exchange_records) {
            break;
         }
      }
      increment_peer_exchange();
      co_await peer_exchange_codec::async_write(framed,
                                                peer_exchange_message{
                                                    .kind = peer_exchange_message::type::peer_exchange_response,
                                                    .request_id = request_id,
                                                    .peer = local,
                                                    .endpoints = std::move(endpoints),
                                                },
                                                codec_for(options));
   }
