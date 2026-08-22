#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge::net::quic::detail {

class client_token_cache {
 public:
   using clock = std::chrono::steady_clock;

   struct limits {
      std::size_t max_entries = 1'024;
      std::size_t max_token_bytes = 512;
      std::size_t max_key_bytes = 512;
      std::size_t max_raw_bytes = 1 * 1024 * 1024;
      std::size_t max_seen_digests = 2'048;
   };

   struct statistics {
      std::size_t entries = 0;
      std::size_t seen_digests = 0;
      std::size_t raw_bytes = 0;
   };

   client_token_cache();
   explicit client_token_cache(limits value);

   [[nodiscard]] std::optional<std::vector<std::uint8_t>> take(std::string_view key,
                                                               clock::time_point now = clock::now());
   [[nodiscard]] bool store(std::string key, std::vector<std::uint8_t> token, clock::time_point now = clock::now());
   [[nodiscard]] statistics snapshot(clock::time_point now = clock::now()) const;

 private:
   struct entry {
      std::vector<std::uint8_t> token;
      clock::time_point expires_at;
      std::uint64_t last_used = 0;
   };

   struct tombstone {
      clock::time_point expires_at;
      std::uint64_t last_used = 0;
   };

   void prune_expired_locked(clock::time_point now) const;
   [[nodiscard]] bool make_room_locked(std::size_t entry_bytes, std::size_t tombstone_bytes);
   void erase_entry_locked(std::map<std::string, entry>::iterator value) const;
   void erase_tombstone_locked(std::map<std::string, tombstone>::iterator value) const;
   [[nodiscard]] std::uint64_t next_lru_locked() const noexcept;

   limits limits_;
   mutable std::mutex mutex_;
   mutable std::map<std::string, entry> entries_;
   mutable std::map<std::string, tombstone> tombstones_;
   mutable std::size_t raw_bytes_ = 0;
   mutable std::uint64_t lru_ = 0;
};

} // namespace forge::net::quic::detail
