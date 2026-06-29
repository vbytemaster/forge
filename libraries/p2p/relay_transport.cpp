module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

module forge.p2p.node;

import forge.crypto.pem;
import forge.crypto.asymmetric;
import forge.p2p.exceptions;
import forge.p2p.identity;
import forge.p2p.stream;
import forge.tcp.connection;
import forge.yamux.session;

#include "details/relay_transport.hxx"
#include "details/stream_upgrade.hxx"

namespace forge::p2p {

void trace_relay(std::string_view message) {
   (void)message;
}

[[nodiscard]] forge::crypto::asymmetric::private_key private_key_from_pem(std::string_view pem) {
   try {
      return forge::crypto::pem::read_private_key(pem);
   } catch (const forge::exceptions::base& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_identity, error.what());
   }
}

boost::asio::awaitable<std::shared_ptr<forge::yamux::session>>
upgrade_relay_outbound_session(forge::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   trace_relay("outbound upgrade: select noise");
   auto upgraded = co_await upgrade_outbound_stream(
       std::move(stream), options, options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("outbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

boost::asio::awaitable<std::shared_ptr<forge::yamux::session>>
upgrade_relay_inbound_session(forge::p2p::stream stream, const node::options& options, const peer_id& expected_peer) {
   trace_relay("inbound upgrade: accept noise");
   auto upgraded = co_await upgrade_inbound_stream(
       std::move(stream), options, options.allow_insecure_test_mode ? std::nullopt : std::make_optional(expected_peer));
   trace_relay("inbound upgrade: yamux ready");
   co_return std::move(upgraded.session);
}

} // namespace forge::p2p
