module;

#include <fcl/exceptions/macros.hpp>

#include "wrapper_handles.hpp"

#include <memory>
#include <utility>

#include <boost/asio/awaitable.hpp>

module fcl.quic.connector;

import fcl.quic.exceptions;
import fcl.quic.runtime;
import fcl.quic.security;

namespace fcl::quic {
namespace {

[[nodiscard]] exceptions::code map_error(detail::engine_error_kind kind) noexcept {
   switch (kind) {
   case detail::engine_error_kind::invalid_endpoint:
      return exceptions::code::invalid_endpoint;
   case detail::engine_error_kind::invalid_options:
      return exceptions::code::invalid_options;
   case detail::engine_error_kind::dependency_unavailable:
      return exceptions::code::dependency_unavailable;
   case detail::engine_error_kind::connect_timeout:
      return exceptions::code::connect_timeout;
   case detail::engine_error_kind::handshake_timeout:
      return exceptions::code::handshake_timeout;
   case detail::engine_error_kind::idle_timeout:
      return exceptions::code::idle_timeout;
   case detail::engine_error_kind::tls_failed:
      return exceptions::code::tls_failed;
   case detail::engine_error_kind::peer_verification_failed:
      return exceptions::code::peer_verification_failed;
   case detail::engine_error_kind::alpn_mismatch:
      return exceptions::code::alpn_mismatch;
   case detail::engine_error_kind::frame_too_large:
      return exceptions::code::frame_too_large;
   case detail::engine_error_kind::malformed_frame:
      return exceptions::code::malformed_frame;
   case detail::engine_error_kind::backpressure_rejected:
      return exceptions::code::backpressure_rejected;
   case detail::engine_error_kind::connection_closed:
      return exceptions::code::connection_closed;
   case detail::engine_error_kind::stream_closed:
      return exceptions::code::stream_closed;
   case detail::engine_error_kind::stream_reset:
      return exceptions::code::stream_reset;
   case detail::engine_error_kind::canceled:
      return exceptions::code::canceled;
   case detail::engine_error_kind::internal_error:
      return exceptions::code::internal;
   }
   return exceptions::code::internal;
}

[[noreturn]] void raise_engine_failure(const detail::engine_failure& error) {
   FCL_THROW_CODE(map_error(error.kind()), error.what());
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

[[nodiscard]] detail::engine_client_options map_options(const client_options& options) {
   return detail::engine_client_options{
       .alpn = options.alpn,
       .connect_timeout = options.connect_timeout,
       .handshake_timeout = options.handshake_timeout,
       .idle_timeout = options.idle_timeout,
       .limits = map_limits(options.limits),
       .security = map_security(options.security),
       .certificate_pem = options.certificate_pem,
       .private_key_pem = options.private_key_pem,
       .test_failpoint = options.test_failpoint,
   };
}

} // namespace

struct connector::impl {
   explicit impl(fcl::asio::runtime& runtime_value) : runtime(runtime_value), engine(runtime_value.context()) {}

   fcl::asio::runtime& runtime;
   detail::engine_connector engine;
};

connector::connector(fcl::asio::runtime& runtime) : impl_(std::make_unique<impl>(runtime)) {}

connector::~connector() = default;

boost::asio::awaitable<connection> connector::async_connect(endpoint remote, client_options options) {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::canceled, "invalid QUIC connector");
   }
   validate(options);
   const auto capabilities = initialize_runtime();
   if (!capabilities.crypto_ossl_initialized) {
      FCL_THROW_EXCEPTION(exceptions::dependency_unavailable, "ngtcp2 OpenSSL crypto backend initialization failed");
   }
   try {
      auto engine_connection = co_await impl_->engine.async_connect(
          detail::engine_endpoint{.host = std::move(remote.host), .port = remote.port}, map_options(options));
      co_return detail::connection_access::make(detail::connection_handle{.engine = std::move(engine_connection)});
   } catch (const detail::engine_failure& error) {
      raise_engine_failure(error);
   }
}

void connector::cancel() {
   if (impl_) {
      impl_->engine.cancel();
   }
}

} // namespace fcl::quic
