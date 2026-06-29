#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>

namespace forge::p2p {

struct upgraded_session {
   peer_id peer;
   std::shared_ptr<forge::yamux::session> session;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_stream(forge::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_stream(forge::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

struct tcp_upgrade_deadline {
   boost::asio::io_context* context = nullptr;
   std::chrono::milliseconds timeout{0};
   std::shared_ptr<std::function<void()>> cancel_current;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_tcp(forge::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_tcp(forge::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session> upgrade_outbound_tcp(forge::tcp::connection connection,
                                                              const node::options& options,
                                                              std::optional<peer_id> expected_peer,
                                                              tcp_upgrade_deadline deadline);

boost::asio::awaitable<upgraded_session> upgrade_inbound_tcp(forge::tcp::connection connection,
                                                             const node::options& options,
                                                             std::optional<peer_id> expected_peer,
                                                             tcp_upgrade_deadline deadline);

} // namespace forge::p2p
