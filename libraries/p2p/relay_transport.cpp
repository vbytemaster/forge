module;

#include <fcl/exceptions/macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.node;

import fcl.crypto.pem;
import fcl.crypto.asymmetric;
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.p2p.stream;
import fcl.yamux.session;

#include "relay_transport.hpp"
#include "stream_upgrade.hpp"

namespace fcl::p2p {

void trace_relay(std::string_view message) {
   (void)message;
}

[[nodiscard]] fcl::crypto::asymmetric::private_key private_key_from_pem(std::string_view pem) {
   try {
      return fcl::crypto::pem::read_private_key(pem);
   } catch (const fcl::exceptions::base& error) {
      FCL_THROW_EXCEPTION(exceptions::invalid_identity, error.what());
   }
}

boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
upgrade_relay_outbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   trace_relay("outbound upgrade: select noise");
   auto upgraded = co_await upgrade_outbound_stream(
       std::move(stream), options, options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("outbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

boost::asio::awaitable<std::shared_ptr<fcl::yamux::session>>
upgrade_relay_inbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   trace_relay("inbound upgrade: accept noise");
   auto upgraded = co_await upgrade_inbound_stream(
       std::move(stream), options, options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("inbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

} // namespace fcl::p2p
