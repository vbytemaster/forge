module;

#include <algorithm>
#include <cstdint>
#include <string>

module fcl.p2p.resource_manager;

namespace fcl::p2p {

resource_manager::resource_manager() = default;

resource_manager::resource_manager(limits value) : limits_(value) {}

const resource_manager::limits& resource_manager::configured_limits() const noexcept {
   return limits_;
}

resource_manager::snapshot resource_manager::current() const noexcept {
   auto out = snapshot_;
   out.active_peer_scopes = streams_by_peer_.size();
   out.active_protocol_scopes = streams_by_protocol_.size();
   out.active_session_peer_scopes = sessions_by_peer_.size();
   out.dial_attempt_scopes = dial_attempts_by_peer_.size();
   out.malformed_scopes = malformed_by_peer_.size();
   return out;
}

bool resource_manager::deny() noexcept {
   ++snapshot_.denied;
   return false;
}

bool resource_manager::empty(const peer_id& value) noexcept {
   return value.value.empty();
}

bool resource_manager::empty(const protocol_id& value) noexcept {
   return value.value.empty();
}

bool resource_manager::try_acquire_stream() noexcept {
   if (snapshot_.active_streams >= limits_.max_streams) {
      return deny();
   }
   ++snapshot_.active_streams;
   return true;
}

void resource_manager::release_stream() noexcept {
   if (snapshot_.active_streams > 0) {
      --snapshot_.active_streams;
   }
}

bool resource_manager::try_acquire_stream(const scope& value) noexcept {
   if (empty(value.peer) || empty(value.protocol)) {
      return deny();
   }
   const auto peer_streams = streams_by_peer_[value.peer];
   const auto protocol_streams = streams_by_protocol_[value.protocol.value];
   if (snapshot_.active_streams >= limits_.max_streams || peer_streams >= limits_.max_streams_per_peer ||
       protocol_streams >= limits_.max_streams_per_protocol) {
      return deny();
   }
   ++snapshot_.active_streams;
   ++streams_by_peer_[value.peer];
   ++streams_by_protocol_[value.protocol.value];
   return true;
}

void resource_manager::release_stream(const scope& value) noexcept {
   release_stream();
   if (auto it = streams_by_peer_.find(value.peer); it != streams_by_peer_.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         streams_by_peer_.erase(it);
      }
   }
   if (auto it = streams_by_protocol_.find(value.protocol.value); it != streams_by_protocol_.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         streams_by_protocol_.erase(it);
      }
   }
}

bool resource_manager::try_acquire_relay_stream() noexcept {
   if (snapshot_.active_relay_streams >= limits_.max_relay_streams || !try_acquire_stream()) {
      return deny();
   }
   ++snapshot_.active_relay_streams;
   return true;
}

void resource_manager::release_relay_stream() noexcept {
   if (snapshot_.active_relay_streams > 0) {
      --snapshot_.active_relay_streams;
   }
   release_stream();
}

bool resource_manager::try_acquire_relay_reservation(const scope& value) noexcept {
   if (empty(value.peer)) {
      return deny();
   }
   if (snapshot_.active_relay_reservations >= limits_.max_relay_reservations) {
      return deny();
   }
   ++snapshot_.active_relay_reservations;
   ++relay_reservations_by_peer_[value.peer];
   return true;
}

void resource_manager::release_relay_reservation(const scope& value) noexcept {
   if (snapshot_.active_relay_reservations > 0) {
      --snapshot_.active_relay_reservations;
   }
   if (auto it = relay_reservations_by_peer_.find(value.peer); it != relay_reservations_by_peer_.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         relay_reservations_by_peer_.erase(it);
      }
   }
}

bool resource_manager::try_acquire_pending_session(session_direction direction) noexcept {
   auto& current = direction == session_direction::inbound ? snapshot_.pending_inbound_sessions
                                                           : snapshot_.pending_outbound_sessions;
   const auto limit = direction == session_direction::inbound ? limits_.max_pending_inbound_sessions
                                                              : limits_.max_pending_outbound_sessions;
   if (current >= limit) {
      return deny();
   }
   ++current;
   return true;
}

void resource_manager::release_pending_session(session_direction direction) noexcept {
   auto& current = direction == session_direction::inbound ? snapshot_.pending_inbound_sessions
                                                           : snapshot_.pending_outbound_sessions;
   if (current > 0) {
      --current;
   }
}

bool resource_manager::try_acquire_session(const session_scope& value) noexcept {
   if (empty(value.peer)) {
      return deny();
   }
   const auto peer = sessions_by_peer_.find(value.peer);
   const auto peer_sessions = peer == sessions_by_peer_.end() ? 0 : peer->second;
   const auto inbound = value.direction == session_direction::inbound;
   const auto total = snapshot_.active_inbound_sessions + snapshot_.active_outbound_sessions;
   if (total >= limits_.max_inbound_sessions + limits_.max_outbound_sessions ||
       peer_sessions >= limits_.max_sessions_per_peer ||
       (inbound && snapshot_.active_inbound_sessions >= limits_.max_inbound_sessions) ||
       (!inbound && snapshot_.active_outbound_sessions >= limits_.max_outbound_sessions)) {
      return deny();
   }
   ++sessions_by_peer_[value.peer];
   if (inbound) {
      ++snapshot_.active_inbound_sessions;
   } else {
      ++snapshot_.active_outbound_sessions;
   }
   return true;
}

void resource_manager::release_session(const session_scope& value) noexcept {
   if (value.direction == session_direction::inbound) {
      if (snapshot_.active_inbound_sessions > 0) {
         --snapshot_.active_inbound_sessions;
      }
   } else if (snapshot_.active_outbound_sessions > 0) {
      --snapshot_.active_outbound_sessions;
   }
   if (auto it = sessions_by_peer_.find(value.peer); it != sessions_by_peer_.end()) {
      if (it->second > 1) {
         --it->second;
      } else {
         sessions_by_peer_.erase(it);
      }
   }
}

bool resource_manager::try_acquire_dial(const scope& value) noexcept {
   if (empty(value.peer)) {
      return deny();
   }
   auto& attempts = dial_attempts_by_peer_[value.peer];
   if (attempts >= limits_.max_dial_attempts_per_peer) {
      return deny();
   }
   ++attempts;
   return true;
}

bool resource_manager::record_malformed(const scope& value) noexcept {
   if (empty(value.peer)) {
      return deny();
   }
   auto& count = malformed_by_peer_[value.peer];
   if (count >= limits_.max_malformed_messages_per_peer) {
      return deny();
   }
   ++count;
   return true;
}

bool resource_manager::add_relay_bytes(std::uint64_t bytes) noexcept {
   if (bytes > limits_.max_relay_bytes || snapshot_.relay_bytes > limits_.max_relay_bytes - bytes) {
      return deny();
   }
   snapshot_.relay_bytes += bytes;
   return true;
}

} // namespace fcl::p2p
