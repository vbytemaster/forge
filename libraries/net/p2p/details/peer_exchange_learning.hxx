#pragma once

#include <optional>

#include "peer_exchange_codec.hxx"

namespace forge::net::p2p::detail {

void learn_authenticated_peer_exchange_response(
    peer_store& store, const peer_exchange_message& message, const peer_id& authenticated_peer,
    std::optional<forge::net::p2p::endpoint> remote_endpoint = std::nullopt,
    std::chrono::system_clock::time_point observed_at = {},
    std::chrono::system_clock::time_point expires_at = {});

} // namespace forge::net::p2p::detail
