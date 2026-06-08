module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module fcl.p2p.hole_punch;

import fcl.p2p.endpoint;
import fcl.p2p.identity;

export namespace fcl::p2p {

struct hole_punch {
   enum class status : std::uint16_t {
      not_attempted = 0,
      prepared = 1,
      synced = 2,
      succeeded = 3,
      failed = 4,
   };

   struct options {
      std::chrono::milliseconds timeout{10'000};
      std::size_t max_observed_endpoints = 32;
      std::size_t max_message_size = 4 * 1024;
   };

   struct result {
      status value = status::not_attempted;
      std::vector<endpoint> attempted;
   };

   class attempt;

   struct message {
      enum class message_kind : std::uint16_t {
         connect = 100,
         sync = 300,
      };

      message_kind kind = message_kind::connect;
      std::vector<endpoint> observed_endpoints;
   };

   struct codec {
      [[nodiscard]] static std::vector<std::uint8_t> encode(const message& value);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes, options opts);
   };
};

class hole_punch::attempt {
 public:
   peer_id peer;
   peer_id relay_peer;
   std::chrono::milliseconds rtt{0};
   std::size_t max_attempts = 1;

   [[nodiscard]] std::chrono::milliseconds sync_delay() const noexcept {
      return rtt <= std::chrono::milliseconds{0} ? std::chrono::milliseconds{0} : rtt / 2;
   }

   [[nodiscard]] bool try_begin() noexcept {
      if (in_flight_ || attempts_ >= max_attempts) {
         return false;
      }
      in_flight_ = true;
      ++attempts_;
      value_ = status::prepared;
      return true;
   }

   void finish(status value) noexcept {
      in_flight_ = false;
      value_ = value;
   }

   [[nodiscard]] bool can_retry() const noexcept {
      return !in_flight_ && value_ != status::succeeded && attempts_ < max_attempts;
   }

   [[nodiscard]] result result() const {
      return hole_punch::result{.value = value_, .attempted = attempted_};
   }

   void add_attempted(endpoint value) {
      attempted_.push_back(std::move(value));
   }

 private:
   std::size_t attempts_ = 0;
   bool in_flight_ = false;
   status value_ = status::not_attempted;
   std::vector<endpoint> attempted_;
};

} // namespace fcl::p2p
