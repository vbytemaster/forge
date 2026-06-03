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

struct certificate_chain {
   std::vector<peer_certificate> certificates;
};

using peer_verifier = std::function<bool(const certificate_chain&)>;

struct security_options {
   bool verify_peer = true;
   bool require_peer_certificate = false;
   std::string trusted_ca_pem;
   std::optional<std::string> expected_sha256_fingerprint;
   peer_verifier verifier;
};

enum class sni_policy : std::uint8_t {
   endpoint_host,
   explicit_name,
   disabled,
};

struct client_options {
   security_options security;
   std::string certificate_pem;
   std::string private_key_pem;
   std::string server_name;
   sni_policy sni = sni_policy::endpoint_host;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
   bool tls13_only = true;
};

struct server_options {
   security_options security{.verify_peer = false};
   std::string certificate_pem;
   std::string private_key_pem;
   std::vector<std::string> alpn_protocols;
   std::size_t read_chunk_size = 64 * 1024;
   bool tls13_only = true;
};

} // namespace fcl::stcp
