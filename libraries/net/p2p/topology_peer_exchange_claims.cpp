module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.net.p2p.exceptions;
import forge.net.p2p.identify;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;

#include "details/topology_peer_exchange_claims.hxx"

namespace forge::net::p2p::detail {

static_assert(std::is_nothrow_move_constructible_v<peer_exchange_scheduler::claim>);
static_assert(std::is_nothrow_move_constructible_v<topology_peer_exchange_claims>);

topology_peer_exchange_claims::topology_peer_exchange_claims(
    std::mutex& mutex, peer_exchange_scheduler& scheduler, std::vector<peer_exchange_scheduler::claim> pending,
    std::chrono::milliseconds retry_after) noexcept
    : mutex_(mutex), scheduler_(scheduler), pending_(std::move(pending)), retry_after_(retry_after) {}

topology_peer_exchange_claims::topology_peer_exchange_claims(
    std::mutex& mutex, peer_exchange_scheduler& scheduler, peer_exchange_scheduler::claim pending,
    std::chrono::milliseconds retry_after) noexcept
    : mutex_(mutex), scheduler_(scheduler), single_(std::move(pending)), retry_after_(retry_after) {}

topology_peer_exchange_claims::topology_peer_exchange_claims(topology_peer_exchange_claims&& other) noexcept
    : mutex_(other.mutex_), scheduler_(other.scheduler_), single_(std::move(other.single_)),
      pending_(std::move(other.pending_)), staged_(std::move(other.staged_)), retry_after_(other.retry_after_),
      transferred_claims_(other.transferred_claims_), active_(std::exchange(other.active_, false)) {}

topology_peer_exchange_claims::~topology_peer_exchange_claims() noexcept {
   rollback();
}

void topology_peer_exchange_claims::stage() {
   staged_.reserve(pending_.size());
   while (transferred_claims_ != pending_.size()) {
      // Allocation happens before the no-throw claim move. staged_ is fully
      // reserved, so every moved claim is immediately owned by this state.
      auto claim = std::make_shared<peer_exchange_scheduler::claim>(std::move(pending_[transferred_claims_]));
      staged_.emplace_back(std::move(claim));
      ++transferred_claims_;
   }
}

peer_exchange_scheduler::claim& topology_peer_exchange_claims::single_claim() noexcept {
   return *single_;
}

const std::vector<std::shared_ptr<peer_exchange_scheduler::claim>>&
topology_peer_exchange_claims::staged_claims() const noexcept {
   return staged_;
}

void topology_peer_exchange_claims::settle_worker(peer_exchange_scheduler::claim& claim) noexcept {
   try {
      const auto lock = std::scoped_lock{mutex_};
      rollback_claim(claim);
   } catch (...) {
      // Completion delivery must not leave the executor through a terminal path.
   }
}

void topology_peer_exchange_claims::settle() noexcept {
   rollback();
}

void topology_peer_exchange_claims::release() noexcept {
   active_ = false;
}

void topology_peer_exchange_claims::rollback_claim(peer_exchange_scheduler::claim& claim) noexcept {
   try {
      scheduler_.fail(claim, exceptions::code::closed, {}, std::chrono::steady_clock::now(), retry_after_);
      scheduler_.leave(claim);
   } catch (...) {
      // This is a destructor path. Scheduler rollback is best-effort only if
      // an unexpected implementation failure escapes its noexcept boundary.
   }
}

void topology_peer_exchange_claims::rollback() noexcept {
   if (!active_) {
      return;
   }

   try {
      const auto lock = std::scoped_lock{mutex_};
      if (single_) {
         rollback_claim(*single_);
      }
      for (auto index = transferred_claims_; index < pending_.size(); ++index) {
         rollback_claim(pending_[index]);
      }
      for (const auto& claim : staged_) {
         rollback_claim(*claim);
      }
      active_ = false;
   } catch (...) {
      // Never terminate while unwinding an unpublished topology batch.
   }
}

} // namespace forge::net::p2p::detail
