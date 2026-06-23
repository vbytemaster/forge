#pragma once

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

} // namespace forge::p2p
