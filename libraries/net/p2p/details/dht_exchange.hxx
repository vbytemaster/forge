#pragma once

#include <memory>

namespace forge::net::p2p {

class cancellation_latch;

namespace detail {

void validate_dht_request(const dht::message& request, const peer_id& remote, const dht::profile& profile);
void validate_dht_response(const dht::message& request, const dht::message& response, const dht::profile& profile);

boost::asio::awaitable<dht::message> async_exchange_dht(forge::net::p2p::stream stream, dht::message request,
                                                        const dht::profile& profile, boost::asio::io_context& context,
                                                        std::chrono::milliseconds timeout,
                                                        std::shared_ptr<cancellation_latch> cancellation = {});

boost::asio::awaitable<void> async_send_dht(forge::net::p2p::stream stream, dht::message request,
                                            const dht::profile& profile, boost::asio::io_context& context,
                                            std::chrono::milliseconds timeout,
                                            std::shared_ptr<cancellation_latch> cancellation = {});

} // namespace detail
} // namespace forge::net::p2p
