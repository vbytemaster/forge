module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

export module forge.quic.options;

import forge.quic.security;

export namespace forge::quic {

struct transport_limits {
   std::size_t max_connections = 1024;
   std::size_t max_streams_per_connection = 256;
   std::size_t max_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_packets = 4096;
   std::uint64_t max_frame_size = 16 * 1024 * 1024;
};

struct client_options {
   std::string alpn = "forge-p2p/1";
   std::chrono::milliseconds connect_timeout{10'000};
   std::chrono::milliseconds handshake_timeout{10'000};
   std::chrono::milliseconds idle_timeout{30'000};
   transport_limits limits{};
   security_options security{};
   std::string certificate_pem;
   std::string private_key_pem;
   std::function<bool(std::string_view)> test_failpoint;
};

struct server_options {
   std::string alpn = "forge-p2p/1";
   std::chrono::milliseconds handshake_timeout{10'000};
   std::chrono::milliseconds idle_timeout{30'000};
   transport_limits limits{};
   security_options security{.verify_peer = false};
   std::string certificate_pem;
   std::string private_key_pem;
};

void validate(const client_options& options);
void validate(const server_options& options);

} // namespace forge::quic
