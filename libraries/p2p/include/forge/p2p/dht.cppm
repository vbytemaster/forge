module;

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module forge.p2p.dht;

import forge.p2p.endpoint;
import forge.p2p.identity;
import forge.p2p.protocol;

export namespace forge::p2p {

struct dht {
   enum class mode : std::uint16_t {
      client = 0,
      server = 1,
   };

   enum class message_type : std::uint16_t {
      put_value = 0,
      get_value = 1,
      add_provider = 2,
      get_providers = 3,
      find_node = 4,
      ping = 5,
   };

   enum class connection_type : std::uint16_t {
      not_connected = 0,
      connected = 1,
      can_connect = 2,
      cannot_connect = 3,
   };

   struct options {
      mode operating_mode = mode::client;
      std::size_t replication = 20;
      std::size_t alpha = 10;
      std::size_t max_message_size = 1024 * 1024;
      std::size_t max_record_size = 1024 * 1024;
      std::size_t max_closer_peers = 20;
      std::size_t max_provider_peers = 20;
      std::chrono::milliseconds query_timeout{10'000};
      std::chrono::milliseconds refresh_interval{600'000};
      std::chrono::seconds provider_record_ttl{172'800};
   };

   struct key {
      std::vector<std::uint8_t> bytes;

      [[nodiscard]] friend bool operator==(const key&, const key&) noexcept = default;
      [[nodiscard]] friend auto operator<=>(const key&, const key&) noexcept = default;
   };

   struct distance {
      std::array<std::uint8_t, 32> bytes{};

      [[nodiscard]] friend bool operator==(const distance&, const distance&) noexcept = default;
      [[nodiscard]] friend auto operator<=>(const distance&, const distance&) noexcept = default;
   };

   struct record {
      key key_value;
      std::vector<std::uint8_t> value;
      std::string time_received;
      std::optional<peer_id> publisher;
      std::chrono::seconds ttl{0};
   };

   struct peer {
      peer_id id;
      std::vector<endpoint> endpoints;
      connection_type connection = connection_type::not_connected;
   };

   struct message {
      message_type type = message_type::find_node;
      std::int32_t cluster_level_raw = 0;
      key key_value;
      std::optional<record> record_value;
      std::vector<peer> closer_peers;
      std::vector<peer> provider_peers;
   };

   struct query_result {
      key target;
      std::vector<peer> closest_peers;
      std::vector<peer> provider_peers;
      std::optional<record> record_value;
      bool complete = false;
   };

   class routing_table;

   struct codec {
      [[nodiscard]] static std::vector<std::uint8_t> encode(const message& value);
      [[nodiscard]] static std::vector<std::uint8_t> encode(const message& value, const options& opts);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes, const options& opts);
   };
};

class dht::routing_table {
 public:
   routing_table(peer_id local_peer, dht::options options_value = {});
   ~routing_table();

   routing_table(const routing_table&) = delete;
   routing_table& operator=(const routing_table&) = delete;

   routing_table(routing_table&&) noexcept;
   routing_table& operator=(routing_table&&) noexcept;

   void upsert(peer value);
   void mark_failure(const peer_id& peer);
   [[nodiscard]] std::vector<peer> closest(std::span<const std::uint8_t> target, std::size_t limit) const;
   [[nodiscard]] std::vector<peer> snapshot() const;

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

[[nodiscard]] dht::key make_dht_key(std::span<const std::uint8_t> value);
[[nodiscard]] dht::key make_dht_key(const peer_id& peer);
[[nodiscard]] dht::distance distance_between(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right);

} // namespace forge::p2p
