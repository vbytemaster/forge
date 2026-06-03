#pragma once

namespace fcl::p2p {

void trace_relay(std::string_view message);
[[nodiscard]] fcl::crypto::asymmetric::private_key private_key_from_pem(std::string_view pem);

class relay_secure_io;

boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
upgrade_relay_outbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
upgrade_relay_inbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

} // namespace fcl::p2p
