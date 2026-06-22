module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

module fcl.quic.security;

import fcl.crypto.hex;
import fcl.crypto.sha256;
import fcl.crypto.x509;

namespace fcl::quic {
namespace {

[[nodiscard]] bool is_hex(char value) noexcept {
   return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

} // namespace

std::string normalize_sha256_fingerprint(std::string_view value) {
   auto normalized = std::string{};
   normalized.reserve(value.size());
   for (const auto ch : value) {
      if (ch == ':' || ch == '-' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
         continue;
      }
      if (!is_hex(ch)) {
         FCL_THROW_EXCEPTION(exceptions::invalid_options, "invalid SHA-256 fingerprint");
      }
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
   }
   if (normalized.size() != 64) {
      FCL_THROW_EXCEPTION(exceptions::invalid_options, "SHA-256 fingerprint must contain 32 bytes");
   }
   return normalized;
}

std::string sha256_fingerprint(std::span<const std::uint8_t> data) {
   const auto digest = fcl::crypto::sha256::hash(data).to_uint8_span();
   return fcl::crypto::to_hex(digest.data(), static_cast<std::uint32_t>(digest.size()));
}

std::string certificate_sha256_fingerprint_from_pem(std::string_view certificate_pem) {
   try {
      return fcl::crypto::x509::certificate::from_pem(certificate_pem).fingerprint_sha256_text();
   } catch (const fcl::exceptions::base& error) {
      FCL_THROW_EXCEPTION(exceptions::tls_failed, error.what());
   }
}

bool verify_peer_certificate(const peer_certificate& certificate, const security_options& options) {
   if (!options.verify_peer) {
      return true;
   }

   const auto actual = normalize_sha256_fingerprint(
       certificate.sha256_fingerprint.empty() ? sha256_fingerprint(certificate.der) : certificate.sha256_fingerprint);
   if (options.expected_sha256_fingerprint) {
      if (actual != normalize_sha256_fingerprint(*options.expected_sha256_fingerprint)) {
         return false;
      }
   }

   if (options.verifier && !options.verifier(certificate)) {
      return false;
   }

   return true;
}

} // namespace fcl::quic
