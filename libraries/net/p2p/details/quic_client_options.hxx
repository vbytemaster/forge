#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace forge::net::p2p::direct::detail {

class quic_client_token_cache;

[[nodiscard]] forge::net::quic::security_options make_quic_peer_verifier(std::optional<peer_id> expected,
                                                                         bool allow_insecure_test_mode);

[[nodiscard]] forge::net::quic::client_options
make_quic_client_options(const forge::net::p2p::endpoint& endpoint, std::optional<peer_id> expected,
                         std::chrono::milliseconds timeout, forge::net::quic::transport_limits limits,
                         std::string certificate_pem, std::string private_key_pem, bool allow_insecure_test_mode,
                         const std::shared_ptr<quic_client_token_cache>& client_tokens);

} // namespace forge::net::p2p::direct::detail
