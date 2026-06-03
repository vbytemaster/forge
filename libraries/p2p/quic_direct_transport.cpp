module;

#include <fcl/exceptions/macros.hpp>

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
import fcl.multiformats;
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
   return fcl::quic::from_transport_endpoint(value.transport);
}

[[nodiscard]] fcl::p2p::endpoint p2p_endpoint_for(const fcl::quic::endpoint& value) {
   return fcl::p2p::endpoint{.transport = fcl::quic::to_transport_endpoint(value)};
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

[[noreturn]] void rethrow_quic_as_p2p(const fcl::exceptions::base& error) {
   const auto code = fcl::quic::exceptions::code_of(error);
   if (code) {
      FCL_THROW_CODE(map_quic_error(*code), error.what());
   }
   throw;
}

[[nodiscard]] peer_id insecure_legacy_peer_id(std::span<const std::uint8_t> der) {
   return peer_id::from_bytes(fcl::multiformats::multihash::sha2_256(der).encode());
}

[[nodiscard]] peer_id strict_peer_id_from_certificate_der(std::span<const std::uint8_t> der) {
   try {
      return make_peer_id_from_certificate_der(der);
   } catch (const fcl::exceptions::base&) {
      FCL_THROW_EXCEPTION(exceptions::peer_verification_failed,
                          "P2P peer certificate is missing a valid signed libp2p identity extension");
   }
}

[[nodiscard]] peer_id verified_peer_id_for(const fcl::quic::connection& connection,
                                           const std::optional<peer_id>& expected, bool insecure_test_mode) {
   if (insecure_test_mode) {
      if (expected) {
         return *expected;
      }
      if (const auto certificate = connection.peer_certificate()) {
         try {
            return make_peer_id_from_certificate_der(certificate->der);
         } catch (const fcl::exceptions::base&) {
            // Insecure test mode still accepts legacy certificates without the libp2p extension.
         }
         return insecure_legacy_peer_id(certificate->der);
      }
      return peer_id{.value = "insecure-test-peer"};
   }

   const auto certificate = connection.peer_certificate();
   if (!certificate) {
      FCL_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P session has no verified peer certificate");
   }
   const auto remote = strict_peer_id_from_certificate_der(certificate->der);
   if (expected && remote != *expected) {
      FCL_THROW_EXCEPTION(exceptions::peer_verification_failed, "P2P peer id does not match expected peer");
   }
   return remote;
}

[[nodiscard]] std::optional<peer_id> expected_peer_for(const fcl::p2p::endpoint& endpoint,
                                                       const node::connect_options& options) {
   if (options.expected_peer) {
      return options.expected_peer;
   }
   return endpoint.peer;
}

class quic_profile final {
 public:
   quic_profile(fcl::asio::runtime& runtime_value, const node::options& options_value)
       : runtime_(runtime_value), options_(options_value), connector_(runtime_value) {}

   [[nodiscard]] bool supports(const fcl::p2p::endpoint& endpoint) const noexcept {
      return endpoint.is_direct_quic();
   }

   [[nodiscard]] bool listening() const noexcept {
      return listener_ != nullptr;
   }

   [[nodiscard]] std::optional<fcl::p2p::endpoint> local_endpoint() const {
      if (!listening()) {
         return std::nullopt;
      }
      return p2p_endpoint_for(listener_->local_endpoint());
   }

   void listen(fcl::p2p::endpoint endpoint) {
      try {
         listener_ = std::make_unique<fcl::quic::listener>(runtime_, quic_endpoint_for(endpoint), server_options());
      } catch (const fcl::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   void stop() {
      if (listener_) {
         listener_->stop();
      }
   }

   boost::asio::awaitable<connection> async_connect(fcl::p2p::endpoint endpoint,
                                                    const node::connect_options& options) {
      try {
         const auto expected_peer = expected_peer_for(endpoint, options);
         auto quic = co_await connector_.async_connect(quic_endpoint_for(endpoint),
                                                       client_options(expected_peer, options.timeout));
         const auto remote = verified_peer_id_for(quic, expected_peer, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         co_return connection{
             .peer = remote,
             .session = fcl::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const fcl::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

   boost::asio::awaitable<connection> async_accept() {
      try {
         auto quic = co_await listener_->async_accept();
         const auto remote = verified_peer_id_for(quic, std::nullopt, options_.allow_insecure_test_mode);
         auto local_endpoint = p2p_endpoint_for(quic.local_endpoint());
         auto remote_endpoint = p2p_endpoint_for(quic.remote_endpoint());
         co_return connection{
             .peer = remote,
             .session = fcl::quic::as_transport_session(std::move(quic)),
             .local_endpoint = std::move(local_endpoint),
             .remote_endpoint = std::move(remote_endpoint),
         };
      } catch (const fcl::exceptions::base& error) {
         rethrow_quic_as_p2p(error);
      }
   }

 private:
   [[nodiscard]] fcl::quic::security_options peer_verifier(std::optional<peer_id> expected = std::nullopt) const {
      if (options_.allow_insecure_test_mode) {
         auto security = fcl::quic::security_options{.verify_peer = true};
         security.verifier = [](const fcl::quic::peer_certificate&) { return true; };
         return security;
      }
      auto security = fcl::quic::security_options{.verify_peer = true};
      security.verifier = [expected = std::move(expected)](const fcl::quic::peer_certificate& certificate) {
         try {
            const auto remote = make_peer_id_from_certificate_der(certificate.der);
            if (expected) {
               return remote == *expected;
            }
            return valid_peer_id(remote);
         } catch (const fcl::exceptions::base&) {
            return false;
         }
      };
      return security;
   }

   [[nodiscard]] fcl::quic::client_options client_options(std::optional<peer_id> expected,
                                                          std::chrono::milliseconds timeout) const {
      return fcl::quic::client_options{
          .alpn = "libp2p",
          .connect_timeout = timeout,
          .handshake_timeout = timeout,
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(std::move(expected)),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
      };
   }

   [[nodiscard]] fcl::quic::server_options server_options() const {
      return fcl::quic::server_options{
          .alpn = "libp2p",
          .limits = quic_limits(options_.transport_limits),
          .security = peer_verifier(),
          .certificate_pem = options_.certificate_pem,
          .private_key_pem = options_.private_key_pem,
      };
   }

   fcl::asio::runtime& runtime_;
   const node::options& options_;
   fcl::quic::connector connector_;
   std::unique_ptr<fcl::quic::listener> listener_;
};

} // namespace

void register_quic_profile(registry& value, fcl::asio::runtime& runtime, const node::options& options) {
   auto owned = std::make_shared<quic_profile>(runtime, options);
   value.add(profile{
       .supports = [owned](const fcl::p2p::endpoint& endpoint) { return owned->supports(endpoint); },
       .listening = [owned] { return owned->listening(); },
       .local_endpoint = [owned] { return owned->local_endpoint(); },
       .listen = [owned](fcl::p2p::endpoint endpoint) { owned->listen(std::move(endpoint)); },
       .stop = [owned] { owned->stop(); },
       .async_connect =
           [owned](fcl::p2p::endpoint endpoint, const node::connect_options& options) {
              return owned->async_connect(std::move(endpoint), options);
           },
       .async_accept = [owned] { return owned->async_accept(); },
   });
}

} // namespace fcl::p2p::direct
