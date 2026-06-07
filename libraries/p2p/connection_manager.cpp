module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.p2p.node;

import fcl.p2p.identity;
import fcl.p2p.resource_manager;

#include "connection_manager.hpp"

namespace fcl::p2p {

connection_manager::connection_manager(policy value) : policy_(value) {}

const connection_manager::policy& connection_manager::configured_policy() const noexcept {
   return policy_;
}

void connection_manager::protect(const peer_id& peer, std::string tag) {
   if (peer.value.empty() || tag.empty()) {
      return;
   }
   protected_[peer].insert(std::move(tag));
}

bool connection_manager::unprotect(const peer_id& peer, std::string_view tag) {
   const auto found = protected_.find(peer);
   if (found == protected_.end()) {
      return false;
   }
   if (!tag.empty()) {
      found->second.erase(std::string{tag});
   }
   if (tag.empty() || found->second.empty()) {
      protected_.erase(found);
      return false;
   }
   return true;
}

bool connection_manager::is_protected(const peer_id& peer) const {
   const auto found = protected_.find(peer);
   return found != protected_.end() && !found->second.empty();
}

namespace {

[[nodiscard]] resource_manager::session_direction resource_direction(connection_manager::direction value) noexcept {
   return value == connection_manager::direction::inbound ? resource_manager::session_direction::inbound
                                                          : resource_manager::session_direction::outbound;
}

[[nodiscard]] resource_manager::session_scope resource_scope(const connection_manager::session_record& record) {
   return resource_manager::session_scope{
       .peer = record.peer,
       .direction = resource_direction(record.direction),
   };
}

} // namespace

void connection_manager::release_record(const session_record& record, resource_manager& resources) {
   resources.release_session(resource_scope(record));
}

void connection_manager::erase_record(std::uint64_t id) {
   const auto found = sessions_.find(id);
   if (found == sessions_.end()) {
      return;
   }
   if (auto peer = sessions_by_peer_.find(found->second.peer); peer != sessions_by_peer_.end()) {
      peer->second.erase(id);
      if (peer->second.empty()) {
         sessions_by_peer_.erase(peer);
      }
   }
   sessions_.erase(found);
}

bool connection_manager::prune_one(resource_manager& resources, std::vector<std::uint64_t>& pruned,
                                   std::chrono::steady_clock::time_point now,
                                   std::optional<direction> required_direction) {
   auto victim = sessions_.end();
   for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
      if (required_direction && it->second.direction != *required_direction) {
         continue;
      }
      if (is_protected(it->second.peer) || now - it->second.opened_at < policy_.grace_period) {
         continue;
      }
      if (victim == sessions_.end() || it->second.last_used_at < victim->second.last_used_at ||
          (it->second.last_used_at == victim->second.last_used_at && it->second.opened_at < victim->second.opened_at)) {
         victim = it;
      }
   }
   if (victim == sessions_.end()) {
      return false;
   }
   const auto id = victim->first;
   release_record(victim->second, resources);
   pruned.push_back(id);
   erase_record(id);
   return true;
}

std::size_t connection_manager::count_peer_sessions(const peer_id& peer) const {
   const auto found = sessions_by_peer_.find(peer);
   return found == sessions_by_peer_.end() ? 0 : found->second.size();
}

std::size_t connection_manager::count_direction_sessions(direction value) const {
   auto count = std::size_t{};
   for (const auto& [_, session] : sessions_) {
      if (session.direction == value) {
         ++count;
      }
   }
   return count;
}

connection_manager::admission connection_manager::remember(session_record record, resource_manager& resources,
                                                           std::chrono::steady_clock::time_point now) {
   if (record.peer.value.empty()) {
      return admission{.accepted = false, .reason = "P2P session peer id is empty"};
   }
   if (record.id == 0) {
      return admission{.accepted = false, .reason = "P2P session id is empty"};
   }
   if (sessions_.contains(record.id)) {
      return admission{.accepted = false, .reason = "P2P duplicate session id"};
   }
   if (record.opened_at == std::chrono::steady_clock::time_point{}) {
      record.opened_at = now;
   }
   if (record.last_used_at == std::chrono::steady_clock::time_point{}) {
      record.last_used_at = record.opened_at;
   }

   const auto& limits = resources.configured_limits();
   const auto direction_limit =
       record.direction == direction::inbound ? limits.max_inbound_sessions : limits.max_outbound_sessions;
   if (count_peer_sessions(record.peer) >= limits.max_sessions_per_peer) {
      return admission{.accepted = false, .reason = "P2P session resource limit reached"};
   }

   auto result = admission{};
   if (sessions_.size() >= policy_.max_sessions) {
      const auto may_prune = !last_prune_ || now - *last_prune_ >= policy_.prune_silence;
      if (count_direction_sessions(record.direction) >= direction_limit &&
          (!may_prune || sessions_.size() <= policy_.low_watermark ||
           !prune_one(resources, result.pruned, now, record.direction))) {
         return admission{.accepted = false, .reason = "P2P session resource limit reached"};
      }
      while (may_prune && sessions_.size() > policy_.low_watermark && prune_one(resources, result.pruned, now)) {
      }
      if (!result.pruned.empty()) {
         last_prune_ = now;
      }
      if (sessions_.size() >= policy_.max_sessions) {
         return admission{.accepted = false, .pruned = std::move(result.pruned),
                          .reason = "P2P max sessions reached"};
      }
   }

   if (!resources.try_acquire_session(resource_scope(record))) {
      return admission{.accepted = false, .pruned = std::move(result.pruned),
                       .reason = "P2P session resource limit reached"};
   }

   const auto id = record.id;
   const auto peer = record.peer;
   sessions_.emplace(id, std::move(record));
   sessions_by_peer_[peer].insert(id);
   result.accepted = true;
   return result;
}

void connection_manager::forget(std::uint64_t id, resource_manager& resources) {
   if (auto found = sessions_.find(id); found != sessions_.end()) {
      release_record(found->second, resources);
      erase_record(id);
   }
}

void connection_manager::forget_peer(const peer_id& peer, resource_manager& resources) {
   auto found = sessions_by_peer_.find(peer);
   if (found == sessions_by_peer_.end()) {
      return;
   }
   auto ids = std::vector<std::uint64_t>{found->second.begin(), found->second.end()};
   for (const auto id : ids) {
      forget(id, resources);
   }
}

void connection_manager::touch(std::uint64_t id, std::chrono::steady_clock::time_point now) {
   if (auto found = sessions_.find(id); found != sessions_.end()) {
      found->second.last_used_at = now;
   }
}

void connection_manager::clear(resource_manager& resources) {
   for (const auto& [_, record] : sessions_) {
      release_record(record, resources);
   }
   sessions_.clear();
   sessions_by_peer_.clear();
}

connection_manager::snapshot connection_manager::current(std::size_t max_sessions) const {
   auto out = snapshot{
      .active_sessions = sessions_.size(),
   };
   out.protected_peers.reserve(protected_.size());
   for (const auto& [peer, tags] : protected_) {
      if (!tags.empty()) {
         out.protected_peers.push_back(peer);
      }
   }
   out.sessions.reserve(std::min(max_sessions, sessions_.size()));
   for (const auto& [_, record] : sessions_) {
      if (out.sessions.size() >= max_sessions) {
         break;
      }
      out.sessions.push_back(record);
   }
   return out;
}

std::size_t connection_manager::size() const noexcept {
   return sessions_.size();
}

connection_manager::policy connection_policy_for(const node::limits& limits) {
   return connection_manager::policy{
       .max_sessions = limits.max_sessions,
       .low_watermark = limits.session_low_watermark,
       .grace_period = limits.session_grace_period,
       .prune_silence = limits.session_prune_silence,
   };
}

} // namespace fcl::p2p
