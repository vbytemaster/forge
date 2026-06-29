#pragma once

namespace forge::p2p {

void trace_relay(std::string_view message);
[[nodiscard]] forge::crypto::asymmetric::private_key private_key_from_pem(std::string_view pem);

class relay_secure_io;

boost::asio::awaitable<std::shared_ptr<forge::yamux::session>>
upgrade_relay_outbound_session(forge::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

boost::asio::awaitable<std::shared_ptr<forge::yamux::session>>
upgrade_relay_inbound_session(forge::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

} // namespace forge::p2p
