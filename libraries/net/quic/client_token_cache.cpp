#include "details/client_token_cache.hxx"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

import forge.crypto.digest.sha256;

namespace forge::net::quic::detail {
namespace {

constexpr auto token_cache_lifetime = std::chrono::minutes{55};
constexpr auto digest_bytes = std::size_t{forge::crypto::digest::sha256::byte_size};

[[nodiscard]] std::string token_digest(std::span<const std::uint8_t> token) {
   const auto digest = forge::crypto::digest::sha256::hash(token);
   return std::string{digest.data(), digest.data_size()};
}

template <typename Map> [[nodiscard]] typename Map::iterator oldest_entry(Map& values) {
   return std::ranges::min_element(values, [](const auto& left, const auto& right) {
      if (left.second.last_used != right.second.last_used) {
         return left.second.last_used < right.second.last_used;
      }
      return left.first < right.first;
   });
}

} // namespace

client_token_cache::client_token_cache() : client_token_cache(limits{}) {}

client_token_cache::client_token_cache(limits value) : limits_(value) {
   if (limits_.max_entries == 0 || limits_.max_token_bytes == 0 || limits_.max_key_bytes == 0 ||
       limits_.max_raw_bytes == 0 || limits_.max_seen_digests == 0) {
      throw std::invalid_argument{"invalid QUIC client token cache limits"};
   }
}

std::optional<std::vector<std::uint8_t>> client_token_cache::take(std::string_view key, clock::time_point now) {
   if (key.empty() || key.size() > limits_.max_key_bytes) {
      return std::nullopt;
   }
   auto lock = std::scoped_lock{mutex_};
   prune_expired_locked(now);
   const auto found = entries_.find(std::string{key});
   if (found == entries_.end()) {
      return std::nullopt;
   }
   const auto entry_bytes = found->first.size() + found->second.token.size();
   auto token = std::move(found->second.token);
   raw_bytes_ -= entry_bytes;
   entries_.erase(found);
   return token;
}

bool client_token_cache::store(std::string key, std::vector<std::uint8_t> token, clock::time_point now) {
   if (key.empty() || key.size() > limits_.max_key_bytes || token.empty() || token.size() > limits_.max_token_bytes) {
      return false;
   }
   const auto digest = token_digest(token);
   auto lock = std::scoped_lock{mutex_};
   prune_expired_locked(now);
   if (tombstones_.contains(digest)) {
      return false;
   }

   const auto existing = entries_.find(key);
   const auto entry_bytes = key.size() + token.size();
   if (entry_bytes > limits_.max_raw_bytes || digest_bytes > limits_.max_raw_bytes - entry_bytes) {
      return false;
   }
   if (existing != entries_.end()) {
      erase_entry_locked(existing);
   }
   if (!make_room_locked(entry_bytes, digest_bytes)) {
      return false;
   }

   const auto expires_at = now + token_cache_lifetime;
   tombstones_.emplace(digest, tombstone{.expires_at = expires_at, .last_used = next_lru_locked()});
   raw_bytes_ += digest_bytes;
   entries_.emplace(std::move(key),
                    entry{.token = std::move(token), .expires_at = expires_at, .last_used = next_lru_locked()});
   raw_bytes_ += entry_bytes;
   return true;
}

client_token_cache::statistics client_token_cache::snapshot(clock::time_point now) const {
   auto lock = std::scoped_lock{mutex_};
   prune_expired_locked(now);
   return {
       .entries = entries_.size(),
       .seen_digests = tombstones_.size(),
       .raw_bytes = raw_bytes_,
   };
}

void client_token_cache::prune_expired_locked(clock::time_point now) const {
   for (auto iterator = entries_.begin(); iterator != entries_.end();) {
      if (iterator->second.expires_at > now) {
         ++iterator;
         continue;
      }
      const auto current = iterator++;
      erase_entry_locked(current);
   }
   for (auto iterator = tombstones_.begin(); iterator != tombstones_.end();) {
      if (iterator->second.expires_at > now) {
         ++iterator;
         continue;
      }
      const auto current = iterator++;
      erase_tombstone_locked(current);
   }
}

bool client_token_cache::make_room_locked(std::size_t entry_bytes, std::size_t tombstone_bytes) {
   const auto required_bytes = entry_bytes + tombstone_bytes;
   if (required_bytes > limits_.max_raw_bytes) {
      return false;
   }
   while (entries_.size() >= limits_.max_entries || tombstones_.size() >= limits_.max_seen_digests ||
          raw_bytes_ > limits_.max_raw_bytes - required_bytes) {
      if (!entries_.empty() &&
          (entries_.size() >= limits_.max_entries || raw_bytes_ > limits_.max_raw_bytes - required_bytes)) {
         erase_entry_locked(oldest_entry(entries_));
         continue;
      }
      if (!tombstones_.empty()) {
         erase_tombstone_locked(oldest_entry(tombstones_));
         continue;
      }
      break;
   }
   return entries_.size() < limits_.max_entries && tombstones_.size() < limits_.max_seen_digests &&
          raw_bytes_ <= limits_.max_raw_bytes - required_bytes;
}

void client_token_cache::erase_entry_locked(std::map<std::string, entry>::iterator value) const {
   raw_bytes_ -= value->first.size() + value->second.token.size();
   entries_.erase(value);
}

void client_token_cache::erase_tombstone_locked(std::map<std::string, tombstone>::iterator value) const {
   raw_bytes_ -= digest_bytes;
   tombstones_.erase(value);
}

std::uint64_t client_token_cache::next_lru_locked() const noexcept {
   if (lru_ == (std::numeric_limits<std::uint64_t>::max)()) {
      lru_ = 0;
      for (auto& [_, entry] : entries_) {
         entry.last_used = ++lru_;
      }
      for (auto& [_, tombstone] : tombstones_) {
         tombstone.last_used = ++lru_;
      }
   }
   return ++lru_;
}

} // namespace forge::net::quic::detail
