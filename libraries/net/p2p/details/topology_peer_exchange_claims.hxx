#pragma once

#include "peer_exchange_scheduler.hxx"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace forge::net::p2p::detail {

class topology_peer_exchange_claims {
 public:
   topology_peer_exchange_claims(std::mutex& mutex, peer_exchange_scheduler& scheduler,
                                 std::vector<peer_exchange_scheduler::claim> pending,
                                 std::chrono::milliseconds retry_after) noexcept;
   topology_peer_exchange_claims(std::mutex& mutex, peer_exchange_scheduler& scheduler,
                                 peer_exchange_scheduler::claim pending,
                                 std::chrono::milliseconds retry_after) noexcept;
   topology_peer_exchange_claims(topology_peer_exchange_claims&& other) noexcept;
   ~topology_peer_exchange_claims() noexcept;

   topology_peer_exchange_claims(const topology_peer_exchange_claims&) = delete;
   topology_peer_exchange_claims& operator=(const topology_peer_exchange_claims&) = delete;
   topology_peer_exchange_claims& operator=(topology_peer_exchange_claims&&) = delete;

   void stage();
   [[nodiscard]] peer_exchange_scheduler::claim& single_claim() noexcept;
   [[nodiscard]] const std::vector<std::shared_ptr<peer_exchange_scheduler::claim>>& staged_claims() const noexcept;
   void settle_worker(peer_exchange_scheduler::claim& claim) noexcept;
   void settle() noexcept;
   void release() noexcept;

 private:
   void rollback_claim(peer_exchange_scheduler::claim& claim) noexcept;
   void rollback() noexcept;

   std::mutex& mutex_;
   peer_exchange_scheduler& scheduler_;
   std::optional<peer_exchange_scheduler::claim> single_;
   std::vector<peer_exchange_scheduler::claim> pending_;
   std::vector<std::shared_ptr<peer_exchange_scheduler::claim>> staged_;
   std::chrono::milliseconds retry_after_;
   std::size_t transferred_claims_ = 0;
   bool active_ = true;
};

} // namespace forge::net::p2p::detail
