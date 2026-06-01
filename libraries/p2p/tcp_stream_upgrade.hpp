#pragma once

#include <optional>

#include <boost/asio/awaitable.hpp>

namespace fcl::p2p {

boost::asio::awaitable<upgraded_session>
upgrade_outbound_tcp(fcl::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_tcp(fcl::tcp::connection connection, const node::options& options, std::optional<peer_id> expected_peer);

} // namespace fcl::p2p
