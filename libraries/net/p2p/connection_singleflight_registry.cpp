module;

#include <cstddef>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>

module forge.net.p2p.node;

import forge.asio.notification;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/connection_singleflight_registry.hxx"
#include "details/lifecycle_wakeup.hxx"

namespace forge::net::p2p::detail {

static_assert(std::is_nothrow_move_assignable_v<connection_singleflight_registry::outcome>);

connection_singleflight_registry::connection_singleflight_registry(test_hooks test_hooks) noexcept
    : test_hooks_(test_hooks) {}

connection_singleflight_registry::entry::entry(std::shared_ptr<lifecycle_wakeup> completion_value) noexcept
    : completion{std::move(completion_value)} {}

connection_singleflight_registry::lease::lease(peer_id peer, std::shared_ptr<entry> owner, bool queued)
    : peer_(std::move(peer)), owner_(std::move(owner)), stop_requested_{std::make_shared<std::atomic_bool>(false)},
      queued_(queued) {}

boost::asio::awaitable<connection_singleflight_registry::outcome> connection_singleflight_registry::lease::wait() {
   const auto owner = owner_;
   if (!owner || !owner->completion) {
      co_return outcome{
          .error = exceptions::code::internal,
          .message = "invalid P2P connection singleflight participant",
      };
   }

   while (true) {
      if (stop_requested_ && stop_requested_->load(std::memory_order_acquire)) {
         throw boost::system::system_error{
             boost::system::error_code{boost::asio::error::operation_aborted}};
      }
      const auto observed = owner->completion->epoch();
      {
         const auto lock = std::scoped_lock{owner->completion_mutex};
         if (stop_requested_ && stop_requested_->load(std::memory_order_acquire)) {
            throw boost::system::system_error{
                boost::system::error_code{boost::asio::error::operation_aborted}};
         }
         if (owner->completed) {
            co_return owner->result;
         }
      }
      // complete() commits the immutable outcome before notifying. A wake that
      // races subscription remains sticky through the notification epoch.
      static_cast<void>(co_await owner->completion->async_wait(observed));
   }
}

void connection_singleflight_registry::lease::request_stop() noexcept {
   if (!stop_requested_ || stop_requested_->exchange(true, std::memory_order_acq_rel)) {
      return;
   }
   if (owner_ && owner_->completion) {
      owner_->completion->notify();
   }
}

connection_singleflight_registry::operation::operation(peer_id peer, std::shared_ptr<entry> owner)
    : peer_(std::move(peer)), owner_(std::move(owner)) {}

connection_singleflight_registry::joined connection_singleflight_registry::join(const peer_id& peer,
                                                                                boost::asio::any_io_executor executor,
                                                                                std::size_t maximum_waiters) {
   static_cast<void>(executor);
   if (closed_) {
      return joined{.status = join_status::closed};
   }
   auto start = std::optional<operation>{};
   auto queued = false;
   auto new_entry = false;
   auto current = std::shared_ptr<entry>{};
   if (const auto found = entries_.find(peer); found != entries_.end()) {
      current = found->second;
      if (queued_participants_ >= maximum_waiters) {
         return joined{.status = join_status::backpressure};
      }
      queued = true;
   } else {
      current = std::make_shared<entry>(std::make_shared<lifecycle_wakeup>());
      new_entry = true;
      start.emplace(operation{peer, current});
   }

   auto participant = lease{peer, current, queued};

   reach_test_failpoint(new_entry ? test_stage::before_new_entry_publish : test_stage::before_existing_entry_commit);

   ++current->participants;
   if (queued) {
      ++queued_participants_;
   }
   if (new_entry) {
      // This is the only potentially throwing commit after all participant state is prepared.
      entries_.emplace(peer, current);
   }
   return joined{
       .status = join_status::accepted,
       .participant = std::move(participant),
       .start = std::move(start),
   };
}

void connection_singleflight_registry::complete(const std::shared_ptr<entry>& owner, outcome result) noexcept {
   auto completion = std::shared_ptr<lifecycle_wakeup>{};
   try {
      {
         const auto lock = std::scoped_lock{owner->completion_mutex};
         if (owner->completed) {
            return;
         }
         owner->result = std::move(result);
         owner->completed = true;
         completion = owner->completion;
      }
      try {
         reach_test_failpoint(test_stage::before_completion_delivery);
      } catch (...) {
         // The committed result remains terminal even if ancillary delivery
         // instrumentation fails.
      }
      if (completion) {
         completion->notify();
      }
   } catch (...) {
      // outcome move assignment is statically no-throw. This guard protects
      // the noexcept lifecycle boundary from unexpected mutex/runtime faults.
   }
}

void connection_singleflight_registry::reach_test_failpoint(test_stage stage) const {
   if (test_hooks_.reach != nullptr) {
      test_hooks_.reach(test_hooks_.context, stage);
   }
}

void connection_singleflight_registry::erase_if_unused(const peer_id& peer,
                                                       const std::shared_ptr<entry>& owner) noexcept {
   const auto found = entries_.find(peer);
   if (!owner->operation_active && owner->participants == 0 && found != entries_.end() && found->second == owner) {
      entries_.erase(found);
   }
}

void connection_singleflight_registry::finish(operation& active, outcome result) noexcept {
   if (!active.owner_) {
      return;
   }
   auto owner = std::move(active.owner_);
   complete(owner, std::move(result));
   owner->operation_active = false;
   erase_if_unused(active.peer_, owner);
}

void connection_singleflight_registry::succeed(operation& active) noexcept {
   finish(active, outcome{.succeeded = true});
}

void connection_singleflight_registry::fail(operation& active, exceptions::code error, std::string message) noexcept {
   finish(active, outcome{.error = error, .message = std::move(message)});
}

void connection_singleflight_registry::rollback_unpublished(operation& active, lease& participant) noexcept {
   if (!active.owner_) {
      return;
   }
   auto owner = std::move(active.owner_);
   owner->operation_active = false;
   if (owner->participants > 1) {
      complete(owner, outcome{.error = exceptions::code::internal});
   }
   leave(participant);
   erase_if_unused(active.peer_, owner);
}

void connection_singleflight_registry::leave(lease& participant) noexcept {
   if (!participant.owner_) {
      return;
   }
   auto owner = std::move(participant.owner_);
   if (owner->participants != 0) {
      --owner->participants;
   }
   if (participant.queued_ && queued_participants_ != 0) {
      --queued_participants_;
   }
   participant.queued_ = false;
   erase_if_unused(participant.peer_, owner);
}

void connection_singleflight_registry::close() noexcept {
   closed_ = true;
   for (const auto& [_, value] : entries_) {
      complete(value, outcome{.error = exceptions::code::closed});
      value->operation_active = false;
   }
   entries_.clear();
}

std::size_t connection_singleflight_registry::size() const noexcept {
   return entries_.size();
}

} // namespace forge::net::p2p::detail
