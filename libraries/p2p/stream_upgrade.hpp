#pragma once

#include <memory>
#include <optional>

#include <boost/asio/awaitable.hpp>

namespace fcl::p2p {

struct upgraded_session {
   peer_id peer;
   std::shared_ptr<fcl::yamux::session> session;
};

boost::asio::awaitable<upgraded_session>
upgrade_outbound_stream(fcl::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

boost::asio::awaitable<upgraded_session>
upgrade_inbound_stream(fcl::p2p::stream stream, const node::options& options, std::optional<peer_id> expected_peer);

} // namespace fcl::p2p
