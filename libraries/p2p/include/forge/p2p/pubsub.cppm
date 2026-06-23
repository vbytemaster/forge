module;

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module forge.p2p.pubsub;

import forge.crypto.asymmetric;
import forge.p2p.identity;
import forge.p2p.protocol;

export namespace forge::p2p::pubsub {

enum class version : std::uint8_t {
   v1_0,
   v1_1,
};

enum class validation_result : std::uint8_t {
   accept,
   reject,
   ignore,
};

enum class signature_policy : std::uint8_t {
   strict_sign,
   strict_no_sign,
   lax_sign,
   lax_no_sign,
};

struct topic {
   std::string value;

   [[nodiscard]] friend bool operator==(const topic&, const topic&) noexcept = default;
   [[nodiscard]] friend auto operator<=>(const topic&, const topic&) noexcept = default;
};

struct limits {
   std::size_t max_rpc_size = 1024 * 1024;
   std::size_t max_message_size = 1024 * 1024;
   std::size_t max_data_size = 1024 * 1024;
   std::size_t max_topic_size = 255;
   std::size_t max_subscriptions = 1024;
   std::size_t max_messages = 1024;
   std::size_t max_control_entries = 1024;
   std::size_t max_message_ids = 5000;
   std::size_t max_peers_per_topic = 12;
   std::size_t max_topics = 1024;
   std::size_t max_validation_queue = 4096;
   std::size_t max_outbound_queue_bytes = 4 * 1024 * 1024;
   std::size_t max_ihave_per_peer = 10;
   std::size_t max_iwant_per_peer = 10;
   std::size_t max_graft_per_peer = 10;
   std::chrono::milliseconds heartbeat_initial_delay{100};
   std::chrono::milliseconds heartbeat_interval{1'000};
   std::chrono::seconds fanout_ttl{60};
   std::chrono::seconds prune_backoff{60};
   std::chrono::seconds unsubscribe_backoff{10};
   std::size_t mesh_n = 6;
   std::size_t mesh_n_low = 5;
   std::size_t mesh_n_high = 12;
   std::size_t mesh_outbound_min = 2;
   std::size_t history_length = 5;
   std::size_t history_gossip = 3;
   std::size_t gossip_lazy = 6;
   double gossip_factor = 0.25;
   std::size_t gossip_retransmission = 3;
};

struct options {
   version preferred = version::v1_1;
   bool allow_v1_0_fallback = true;
   signature_policy signatures = signature_policy::strict_sign;
   limits limits{};
};

struct subscription {
   bool subscribe = true;
   topic subject;
};

struct message {
   std::optional<peer_id> from;
   std::vector<std::uint8_t> data;
   std::vector<std::uint8_t> seqno;
   topic subject;
   std::vector<std::uint8_t> signature;
   std::vector<std::uint8_t> key;
};

struct peer_info {
   peer_id peer;
   std::vector<std::uint8_t> signed_peer_record;
};

struct control {
   struct ihave {
      topic subject;
      std::vector<std::vector<std::uint8_t>> message_ids;
   };

   struct iwant {
      std::vector<std::vector<std::uint8_t>> message_ids;
   };

   struct graft {
      topic subject;
   };

   struct prune {
      topic subject;
      std::vector<peer_info> peers;
      std::chrono::seconds backoff{0};
   };

   std::vector<ihave> have;
   std::vector<iwant> want;
   std::vector<graft> grafts;
   std::vector<prune> prunes;
};

struct rpc {
   std::vector<subscription> subscriptions;
   std::vector<message> messages;
   std::optional<control> control_value;
};

struct publish_options {
   bool sign = true;
};

struct event {
   peer_id source;
   message value;
};

using handler = std::function<boost::asio::awaitable<validation_result>(event)>;

struct score {
   double value = 0.0;
   std::uint64_t invalid_messages = 0;
   std::uint64_t duplicate_messages = 0;
   std::uint64_t delivered_messages = 0;
};

struct snapshot {
   std::size_t topics = 0;
   std::size_t peers = 0;
   std::size_t mesh_edges = 0;
   std::size_t cached_messages = 0;
   std::uint64_t messages_published = 0;
   std::uint64_t messages_received = 0;
   std::uint64_t messages_delivered = 0;
   std::uint64_t duplicates = 0;
   std::uint64_t invalid_messages = 0;
   std::uint64_t control_messages = 0;
};

struct codec {
   [[nodiscard]] static protocol_id protocol(version value);
   [[nodiscard]] static std::vector<std::uint8_t> encode(const rpc& value);
   [[nodiscard]] static std::vector<std::uint8_t> encode(const rpc& value, const options& opts);
   [[nodiscard]] static rpc decode(std::span<const std::uint8_t> bytes);
   [[nodiscard]] static rpc decode(std::span<const std::uint8_t> bytes, const options& opts);
   [[nodiscard]] static std::vector<std::uint8_t> encode_message(const message& value);
   [[nodiscard]] static std::vector<std::uint8_t> signing_payload(const message& value);
   [[nodiscard]] static std::vector<std::uint8_t> message_id(const message& value);
   static void sign_message(message& value, const forge::crypto::asymmetric::private_key& key);
   [[nodiscard]] static bool verify_message(const message& value);
};

} // namespace forge::p2p::pubsub
