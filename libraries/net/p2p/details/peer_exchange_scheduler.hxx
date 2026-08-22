#pragma once

#include "connection_singleflight_registry.hxx"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

namespace forge::net::p2p::detail {

class peer_exchange_scheduler {
 public:
   using clock = std::chrono::steady_clock;

   enum class test_stage : std::uint8_t {
      before_entry_publish,
   };

   struct test_hooks {
      void* context = nullptr;
      void (*reach)(void*, test_stage) = nullptr;
   };

   struct session {
      peer_id peer;
      std::uint64_t session_id = 0;
      identify::state identify_state = identify::state::unknown;
      capability_set capabilities{};
      std::vector<protocol_id> protocols;
   };

   enum class claim_status : std::uint8_t {
      started,
      joined,
      unavailable,
      backoff,
      not_selected,
      closed,
      backpressure,
   };

   struct claim {
      claim_status status = claim_status::unavailable;
      session selected;
      connection_singleflight_registry::lease participant;
      std::optional<connection_singleflight_registry::operation> start;

      [[nodiscard]] bool started() const noexcept;
   };

   [[nodiscard]] static bool eligible(const session& value) noexcept;

   explicit peer_exchange_scheduler(std::size_t maximum_waiters = (std::numeric_limits<std::size_t>::max)()) noexcept;
   peer_exchange_scheduler(std::size_t maximum_waiters, test_hooks test_hooks) noexcept;

   [[nodiscard]] claim claim_next(const std::vector<session>& candidates, clock::time_point now, std::size_t limit,
                                  boost::asio::any_io_executor executor);
   [[nodiscard]] std::vector<claim> claim_batch(const std::vector<session>& candidates, clock::time_point now,
                                                 std::size_t state_limit, std::size_t batch_limit,
                                                 boost::asio::any_io_executor executor);
   [[nodiscard]] claim claim_peer(const peer_id& peer, const std::vector<session>& candidates, clock::time_point now,
                                  std::size_t limit, boost::asio::any_io_executor executor);

   void succeed(claim& active, clock::time_point now, std::chrono::milliseconds retry_after) noexcept;
   void fail(claim& active, exceptions::code error, std::string message, clock::time_point now,
             std::chrono::milliseconds retry_after) noexcept;
   void leave(claim& participant) noexcept;
   void close() noexcept;

   [[nodiscard]] std::size_t size() const noexcept;

 private:
   struct entry {
      clock::time_point retry_after{};
      bool active = false;
   };

   [[nodiscard]] static std::vector<session> normalized(const std::vector<session>& candidates);
   [[nodiscard]] claim begin(const session& selected, boost::asio::any_io_executor executor);
   void expire(clock::time_point now) noexcept;
   void rollback_batch(std::vector<claim>& claims, clock::time_point now) noexcept;
   void reach_test_failpoint(test_stage stage) const;
   void complete(claim& active, connection_singleflight_registry::outcome outcome, clock::time_point now,
                 std::chrono::milliseconds retry_after) noexcept;

   std::map<peer_id, entry> entries_;
   connection_singleflight_registry operations_;
   std::size_t maximum_waiters_ = (std::numeric_limits<std::size_t>::max)();
   bool closed_ = false;
   test_hooks test_hooks_;
};

} // namespace forge::net::p2p::detail
