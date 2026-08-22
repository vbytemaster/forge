#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace forge::net::p2p::detail {

[[nodiscard]] std::optional<std::chrono::seconds> rendezvous_ttl_from_wire(std::uint64_t value) noexcept;

[[nodiscard]] std::chrono::system_clock::time_point
rendezvous_expiry_after(std::chrono::system_clock::time_point now, std::chrono::seconds ttl) noexcept;

} // namespace forge::net::p2p::detail
