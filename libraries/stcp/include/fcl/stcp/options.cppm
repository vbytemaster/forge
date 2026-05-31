module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module fcl.stcp.options;

export namespace fcl::stcp {

struct peer_certificate {
   std::vector<std::uint8_t> der;
   std::string sha256_fingerprint;
};

using peer_verifier = std::function<bool(const peer_certificate&)>;

struct security_options {
   bool verify_peer = true;
   std::string trusted_ca_pem;
   std::optional<std::string> expected_sha256_fingerprint;
   peer_verifier verifier;
};

struct client_options {
   security_options security;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string server_name;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
};

struct server_options {
   security_options security{.verify_peer = false};
   std::string certificate_pem;
   std::string private_key_pem;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
};

} // namespace fcl::stcp
