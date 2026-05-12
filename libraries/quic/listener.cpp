module;

#include "wrapper_handles.hpp"

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.quic.listener;

import fcl.quic.errors;
import fcl.quic.runtime;
import fcl.quic.security;

namespace fcl::quic {
namespace {

[[nodiscard]] error_kind map_error(detail::engine_error_kind kind) noexcept {
   switch (kind) {
   case detail::engine_error_kind::invalid_endpoint:
      return error_kind::invalid_endpoint;
   case detail::engine_error_kind::invalid_options:
      return error_kind::invalid_options;
   case detail::engine_error_kind::dependency_unavailable:
      return error_kind::dependency_unavailable;
   case detail::engine_error_kind::connect_timeout:
      return error_kind::connect_timeout;
   case detail::engine_error_kind::handshake_timeout:
      return error_kind::handshake_timeout;
   case detail::engine_error_kind::idle_timeout:
      return error_kind::idle_timeout;
   case detail::engine_error_kind::tls_failed:
      return error_kind::tls_failed;
   case detail::engine_error_kind::peer_verification_failed:
      return error_kind::peer_verification_failed;
   case detail::engine_error_kind::alpn_mismatch:
      return error_kind::alpn_mismatch;
   case detail::engine_error_kind::frame_too_large:
      return error_kind::frame_too_large;
   case detail::engine_error_kind::malformed_frame:
      return error_kind::malformed_frame;
   case detail::engine_error_kind::backpressure_rejected:
      return error_kind::backpressure_rejected;
   case detail::engine_error_kind::connection_closed:
      return error_kind::connection_closed;
   case detail::engine_error_kind::stream_closed:
      return error_kind::stream_closed;
   case detail::engine_error_kind::stream_reset:
      return error_kind::stream_reset;
   case detail::engine_error_kind::canceled:
      return error_kind::canceled;
   case detail::engine_error_kind::internal_error:
      return error_kind::internal_error;
   }
   return error_kind::internal_error;
}

[[noreturn]] void rethrow_engine_error(const detail::engine_error& error) {
   throw_quic_error(map_error(error.kind()), error.what());
}

[[nodiscard]] detail::engine_transport_limits map_limits(const transport_limits& limits) noexcept {
   return detail::engine_transport_limits{
      .max_connections = limits.max_connections,
      .max_streams_per_connection = limits.max_streams_per_connection,
      .max_queued_bytes = limits.max_queued_bytes,
      .max_inbound_queued_bytes = limits.max_inbound_queued_bytes,
      .max_inbound_queued_packets = limits.max_inbound_queued_packets,
      .max_frame_size = limits.max_frame_size,
   };
}

[[nodiscard]] detail::engine_security_options map_security(const security_options& security) {
   auto mapped = detail::engine_security_options{
      .verify_peer = security.verify_peer,
      .expected_sha256_fingerprint = security.expected_sha256_fingerprint,
      .trusted_ca_pem = security.trusted_ca_pem,
   };
   if (security.verifier) {
      mapped.verifier = [verifier = security.verifier](const detail::engine_peer_certificate& certificate) {
         return verifier(peer_certificate{
            .der = certificate.der,
            .sha256_fingerprint = certificate.sha256_fingerprint,
         });
      };
   }
   return mapped;
}

[[nodiscard]] detail::engine_server_options map_options(const server_options& options) {
   return detail::engine_server_options{
      .alpn = options.alpn,
      .handshake_timeout = options.handshake_timeout,
      .idle_timeout = options.idle_timeout,
      .limits = map_limits(options.limits),
      .security = map_security(options.security),
      .certificate_pem = options.certificate_pem,
      .private_key_pem = options.private_key_pem,
   };
}

} // namespace

struct listener::impl {
   impl(fcl::asio::runtime& runtime_value, endpoint bind_endpoint_value, server_options options_value)
      : runtime(runtime_value)
      , engine(
           runtime_value.context(),
           detail::engine_endpoint{.host = std::move(bind_endpoint_value.host), .port = bind_endpoint_value.port},
           map_options(options_value)) {}

   fcl::asio::runtime& runtime;
   detail::engine_listener engine;
};

listener::listener(fcl::asio::runtime& runtime, endpoint bind_endpoint, server_options options) {
   validate(options);
   const auto capabilities = initialize_runtime();
   if (!capabilities.crypto_ossl_initialized) {
      throw_quic_error(error_kind::dependency_unavailable, "ngtcp2 OpenSSL crypto backend initialization failed");
   }
   impl_ = std::make_unique<impl>(runtime, std::move(bind_endpoint), std::move(options));
}

listener::~listener() = default;

endpoint listener::local_endpoint() const {
   if (!impl_) {
      return endpoint{};
   }
   const auto local = impl_->engine.local_endpoint();
   return endpoint{.host = local.host, .port = local.port};
}

boost::asio::awaitable<connection> listener::async_accept() {
   if (!impl_) {
      throw_quic_error(error_kind::connection_closed, "invalid QUIC listener");
   }
   try {
      auto engine_connection = co_await impl_->engine.async_accept();
      co_return detail::connection_access::make(detail::connection_handle{.engine = std::move(engine_connection)});
   } catch (const detail::engine_error& error) {
      rethrow_engine_error(error);
   }
}

void listener::stop() {
   if (impl_) {
      impl_->engine.stop();
   }
}

} // namespace fcl::quic
