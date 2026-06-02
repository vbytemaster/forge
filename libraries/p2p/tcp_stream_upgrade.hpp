#pragma once

namespace fcl::p2p {

struct tcp_upgrade_deadline {
   boost::asio::io_context* context = nullptr;
   std::chrono::milliseconds timeout{0};
   std::shared_ptr<std::function<void()>> cancel_current;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_tcp(fcl::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_tcp(fcl::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session> upgrade_outbound_tcp(fcl::tcp::connection connection,
                                                              const node::options& options,
                                                              std::optional<peer_id> expected_peer,
                                                              tcp_upgrade_deadline deadline);

boost::asio::awaitable<upgraded_session> upgrade_inbound_tcp(fcl::tcp::connection connection,
                                                             const node::options& options,
                                                             std::optional<peer_id> expected_peer,
                                                             tcp_upgrade_deadline deadline);

} // namespace fcl::p2p
