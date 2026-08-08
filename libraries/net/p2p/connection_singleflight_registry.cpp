module;

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/connection_singleflight_registry.hxx"

namespace forge::net::p2p::detail {

connection_singleflight_registry::lease::lease(peer_id peer, std::shared_ptr<entry> owner,
                                               std::shared_ptr<completion_channel> completion)
    : peer_(std::move(peer)), owner_(std::move(owner)), completion_(std::move(completion)) {}

boost::asio::awaitable<connection_singleflight_registry::outcome> connection_singleflight_registry::lease::wait() {
   if (!completion_) {
      co_return outcome{
          .error = exceptions::code::internal,
          .message = "invalid P2P connection singleflight participant",
      };
   }
   co_return co_await completion_->async_receive(boost::asio::use_awaitable);
}

connection_singleflight_registry::operation::operation(peer_id peer, std::shared_ptr<entry> owner)
    : peer_(std::move(peer)), owner_(std::move(owner)) {}

connection_singleflight_registry::joined connection_singleflight_registry::join(const peer_id& peer,
                                                                                boost::asio::any_io_executor executor) {
   auto& current = entries_[peer];
   auto start = std::optional<operation>{};
   if (!current) {
      current = std::make_shared<entry>();
      start.emplace(operation{peer, current});
   }
   auto completion = std::make_shared<lease::completion_channel>(std::move(executor), 1);
   ++current->participants;
   current->completions.push_back(completion);
   if (current->completed) {
      static_cast<void>(completion->try_send(boost::system::error_code{}, current->result));
   }
   return joined{
       .participant = lease{peer, current, std::move(completion)},
       .start = std::move(start),
   };
}

void connection_singleflight_registry::complete(entry& owner, outcome result) noexcept {
   if (owner.completed) {
      return;
   }
   owner.result = std::move(result);
   owner.completed = true;
   for (auto& pending : owner.completions) {
      if (auto completion = pending.lock()) {
         static_cast<void>(completion->try_send(boost::system::error_code{}, owner.result));
      }
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
   complete(*owner, std::move(result));
   owner->operation_active = false;
   erase_if_unused(active.peer_, owner);
}

void connection_singleflight_registry::succeed(operation& active) noexcept {
   finish(active, outcome{.succeeded = true});
}

void connection_singleflight_registry::fail(operation& active, exceptions::code error, std::string message) noexcept {
   finish(active, outcome{.error = error, .message = std::move(message)});
}

void connection_singleflight_registry::leave(lease& participant) noexcept {
   if (!participant.owner_) {
      return;
   }
   auto owner = std::move(participant.owner_);
   participant.completion_.reset();
   if (owner->participants != 0) {
      --owner->participants;
   }
   erase_if_unused(participant.peer_, owner);
}

void connection_singleflight_registry::close() noexcept {
   for (const auto& [_, value] : entries_) {
      complete(*value, outcome{
                           .error = exceptions::code::closed,
                           .message = "P2P node stopped during connection singleflight",
                       });
      value->operation_active = false;
   }
   entries_.clear();
}

std::size_t connection_singleflight_registry::size() const noexcept {
   return entries_.size();
}

} // namespace forge::net::p2p::detail
