module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fcl.quic.security;

import fcl.quic.errors;

export namespace fcl::quic {

using byte_vector = std::vector<std::uint8_t>;

struct peer_certificate {
   byte_vector der;
   std::string sha256_fingerprint;
};

using peer_verifier = std::function<bool(const peer_certificate&)>;

struct security_options {
   bool verify_peer = true;
   std::optional<std::string> expected_sha256_fingerprint;
   std::string trusted_ca_pem;
   peer_verifier verifier;
};

[[nodiscard]] std::string normalize_sha256_fingerprint(std::string_view value);
[[nodiscard]] std::string sha256_fingerprint(std::span<const std::uint8_t> data);
[[nodiscard]] std::string certificate_sha256_fingerprint_from_pem(std::string_view certificate_pem);
[[nodiscard]] bool verify_peer_certificate(const peer_certificate& certificate, const security_options& options);

} // namespace fcl::quic
