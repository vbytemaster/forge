module;

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

export module fcl.p2p.resource_manager;

import fcl.p2p.identity;
import fcl.p2p.protocol;

export namespace fcl::p2p {

class resource_manager {
 public:
   struct limits {
      std::size_t max_streams = 4096;
      std::size_t max_streams_per_peer = 256;
      std::size_t max_streams_per_protocol = 1024;
      std::size_t max_relay_reservations = 1024;
      std::size_t max_relay_streams = 128;
      std::uint64_t max_relay_bytes = 256 * 1024 * 1024;
      std::uint64_t max_queued_bytes = 16 * 1024 * 1024;
      std::size_t max_dial_attempts_per_peer = 16;
      std::size_t max_malformed_messages_per_peer = 64;
   };

   struct scope {
      peer_id peer;
      protocol_id protocol;
   };

   struct snapshot {
      std::size_t active_streams = 0;
      std::size_t active_relay_streams = 0;
      std::size_t active_relay_reservations = 0;
      std::uint64_t relay_bytes = 0;
      std::size_t active_peer_scopes = 0;
      std::size_t active_protocol_scopes = 0;
      std::size_t dial_attempt_scopes = 0;
      std::size_t malformed_scopes = 0;
      std::uint64_t denied = 0;
   };

   resource_manager();
   explicit resource_manager(limits value);

   [[nodiscard]] const limits& configured_limits() const noexcept;
   [[nodiscard]] snapshot current() const noexcept;
   [[nodiscard]] bool try_acquire_stream() noexcept;
   void release_stream() noexcept;
   [[nodiscard]] bool try_acquire_stream(const scope& value) noexcept;
   void release_stream(const scope& value) noexcept;
   [[nodiscard]] bool try_acquire_relay_stream() noexcept;
   void release_relay_stream() noexcept;
   [[nodiscard]] bool try_acquire_relay_reservation(const scope& value) noexcept;
   void release_relay_reservation(const scope& value) noexcept;
   [[nodiscard]] bool try_acquire_dial(const scope& value) noexcept;
   [[nodiscard]] bool record_malformed(const scope& value) noexcept;
   [[nodiscard]] bool add_relay_bytes(std::uint64_t bytes) noexcept;

 private:
   [[nodiscard]] bool deny() noexcept;
   [[nodiscard]] static bool empty(const peer_id& value) noexcept;
   [[nodiscard]] static bool empty(const protocol_id& value) noexcept;

   limits limits_;
   snapshot snapshot_;
   std::map<peer_id, std::size_t> streams_by_peer_;
   std::map<std::string, std::size_t> streams_by_protocol_;
   std::map<peer_id, std::size_t> relay_reservations_by_peer_;
   std::map<peer_id, std::size_t> dial_attempts_by_peer_;
   std::map<peer_id, std::size_t> malformed_by_peer_;
};

} // namespace fcl::p2p
