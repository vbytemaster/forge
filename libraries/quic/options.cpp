module;

#include <forge/exceptions/macros.hpp>

#include <chrono>
#include <string>

module forge.quic.options;

import forge.quic.exceptions;

namespace forge::quic {
namespace {

void validate_common_alpn(const std::string& alpn) {
   if (alpn.empty() || alpn.size() > 255) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "QUIC ALPN must be 1..255 bytes");
   }
}

void validate_limits(const transport_limits& limits) {
   if (limits.max_connections == 0 || limits.max_streams_per_connection == 0 || limits.max_queued_bytes == 0 ||
       limits.max_inbound_queued_bytes == 0 || limits.max_inbound_queued_packets == 0 || limits.max_frame_size == 0 ||
       limits.max_frame_size > 0xffff'ffffULL) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "invalid QUIC transport limits");
   }
}

void validate_timeout(std::chrono::milliseconds value, const char* name) {
   if (value.count() <= 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, std::string{name} + " must be positive");
   }
}

} // namespace

void validate(const client_options& options) {
   validate_common_alpn(options.alpn);
   validate_limits(options.limits);
   validate_timeout(options.connect_timeout, "connect_timeout");
   validate_timeout(options.handshake_timeout, "handshake_timeout");
   validate_timeout(options.idle_timeout, "idle_timeout");
   if (options.security.expected_sha256_fingerprint) {
      (void)normalize_sha256_fingerprint(*options.security.expected_sha256_fingerprint);
   }
   if (options.certificate_pem.empty() != options.private_key_pem.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "client certificate and private key must be provided together");
   }
}

void validate(const server_options& options) {
   validate_common_alpn(options.alpn);
   validate_limits(options.limits);
   validate_timeout(options.handshake_timeout, "handshake_timeout");
   validate_timeout(options.idle_timeout, "idle_timeout");
   if (options.certificate_pem.empty() || options.private_key_pem.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "server certificate and private key are required");
   }
   if (options.security.expected_sha256_fingerprint) {
      (void)normalize_sha256_fingerprint(*options.security.expected_sha256_fingerprint);
   }
}

} // namespace forge::quic
