module;

#include <fcl/exception/macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.node;

import fcl.asio.runtime;
import fcl.p2p.endpoint;
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.endpoint;
import fcl.quic.exceptions;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.security;
import fcl.quic.transport;
import fcl.transport.limits;
import fcl.transport.session;

#include "direct_transport.hpp"

namespace fcl::p2p::direct {
namespace {

[[nodiscard]] fcl::quic::transport_limits quic_limits(const fcl::transport::limits& value) noexcept {
   return fcl::quic::transport_limits{
       .max_connections = value.max_connections,
       .max_streams_per_connection = value.max_streams_per_connection,
       .max_queued_bytes = value.max_queued_bytes,
       .max_inbound_queued_bytes = value.max_inbound_queued_bytes,
       .max_inbound_queued_packets = value.max_inbound_queued_packets,
       .max_frame_size = value.max_frame_size,
   };
}

[[nodiscard]] fcl::quic::endpoint quic_endpoint_for(const fcl::p2p::endpoint& value) {
   if (!value.is_direct_quic()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_protocol, "P2P endpoint is not a direct QUIC endpoint");
   }
   return fcl::quic::from_transport_endpoint(value.address);
}

[[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(const fcl::quic::endpoint& value) {
   return fcl::p2p::endpoint{.address = fcl::quic::to_transport_endpoint(value)};
}

[[nodiscard]] exceptions::code map_quic_error(fcl::quic::exceptions::code kind) noexcept {
   using quic_kind = fcl::quic::exceptions::code;
   switch (kind) {
   case quic_kind::invalid_endpoint:
   case quic_kind::invalid_options:
      return exceptions::code::invalid_options;
   case quic_kind::connect_timeout:
   case quic_kind::handshake_timeout:
   case quic_kind::idle_timeout:
      return exceptions::code::timeout;
   case quic_kind::peer_verification_failed:
   case quic_kind::alpn_mismatch:
   case quic_kind::tls_failed:
      return exceptions::code::peer_verification_failed;
   case quic_kind::frame_too_large:
   case quic_kind::malformed_frame:
      return exceptions::code::codec_error;
   case quic_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case quic_kind::connection_closed:
   case quic_kind::stream_closed:
   case quic_kind::stream_reset:
      return exceptions::code::closed;
   case quic_kind::canceled:
      return exceptions::code::canceled;
   case quic_kind::dependency_unavailable:
   case quic_kind::internal:
   case quic_kind::unsupported:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void rethrow_quic_as_p2p(const fcl::exception::base& error) {
   const auto code = fcl::quic::exceptions::code_of(error);
   if (code) {
      FCL_THROW_CODE(map_quic_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] peer_id verified_peer_id_for(const fcl::quic::connection& connection,
                                           const std::optional<peer_id>& expected, bool insecure_test_mode) {
   if (insecure_test_mode) {
      if (expected) {
         return *expected;
      }
      if (const auto certificate = connection.peer_certificate()) {
         return make_peer_id_from_certificate_der(certificate->der);
      }
      return peer_id{.value = "insecure-test-peer"};
   }

   const auto certificate = connection.peer_certificate();
   if (!certificate) {
      FCL_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P session has no verified peer certificate");
   }
   const auto remote = make_peer_id_from_certificate_der(certificate->der);
   if (expected && remote != *expected) {
      FCL_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P peer id does not match expected peer");
   }
   return remote;
}

} // namespace

struct driver::state {
   state(fcl::asio::runtime& runtime_value, const node::options& options_value)
       : runtime(runtime_value), options(options_value), connector(runtime_value) {}

   [[nodiscard]] fcl::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options.allow_insecure_test_mode) {
         auto security = fcl::quic::security_options{.verify_peer = true};
         security.verifier = [](const fcl::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = fcl::quic::security_options{.verify_peer = true};
      if (expected) {
         security.expected_sha256_fingerprint = expected->value;
      } else {
         security.verifier = [](const fcl::quic::peer_certificate& certificate) {
            return valid_peer_id(make_peer_id_from_certificate_der(certificate.der));
         };
      }
      return security;
   }

   [[nodiscard]] fcl::quic::client_options client_options(std::optional<peer_id> expected,
                                                          std::chrono::milliseconds timeout) const {
      return fcl::quic::client_options{
          .alpn = "libp2p",
          .connect_timeout = timeout,
          .handshake_timeout = timeout,
          .limits = quic_limits(options.transport_limits),
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   [[nodiscard]] fcl::quic::server_options server_options() const {
      return fcl::quic::server_options{
          .alpn = "libp2p",
          .limits = quic_limits(options.transport_limits),
          .security = peer_verifier(),
          .certificate_pem = options.certificate_pem,
          .private_key_pem = options.private_key_pem,
      };
   }

   fcl::asio::runtime& runtime;
   const node::options& options;
   fcl::quic::connector connector;
   std::unique_ptr<fcl::quic::listener> listener;
};

driver::driver(fcl::asio::runtime& runtime, const node::options& options) : state_(std::make_unique<state>(runtime, options)) {}

driver::~driver() = default;

bool driver::listening() const noexcept {
   return state_ && state_->listener != nullptr;
}

std::optional<fcl::p2p::endpoint> driver::local_endpoint() const {
   if (!listening()) {
      return std::nullopt;
   }
   return p2p_endpoint_for(state_->listener->local_endpoint());
}

void driver::listen(fcl::p2p::endpoint endpoint) {
   try {
      state_->listener = std::make_unique<fcl::quic::listener>(state_->runtime, quic_endpoint_for(endpoint),
                                                               state_->server_options());
   } catch (const fcl::exception::base& error) {
      rethrow_quic_as_p2p(error);
   }
}

void driver::stop() {
   if (listening()) {
      state_->listener->stop();
   }
}

boost::asio::awaitable<dial_result> driver::async_connect(fcl::p2p::endpoint endpoint,
                                                          const node::connect_options& options) {
   try {
      auto connection =
          co_await state_->connector.async_connect(quic_endpoint_for(endpoint),
                                                   state_->client_options(options.expected_peer, options.timeout));
      const auto remote = verified_peer_id_for(connection, options.expected_peer, state_->options.allow_insecure_test_mode);
      co_return dial_result{.peer = remote, .session = fcl::quic::as_transport_session(std::move(connection))};
   } catch (const fcl::exception::base& error) {
      rethrow_quic_as_p2p(error);
   }
}

boost::asio::awaitable<accepted_connection> driver::async_accept() {
   try {
      auto connection = co_await state_->listener->async_accept();
      const auto remote = verified_peer_id_for(connection, std::nullopt, state_->options.allow_insecure_test_mode);
      co_return accepted_connection{.peer = remote, .session = fcl::quic::as_transport_session(std::move(connection))};
   } catch (const fcl::exception::base& error) {
      rethrow_quic_as_p2p(error);
   }
}

} // namespace fcl::p2p::direct
