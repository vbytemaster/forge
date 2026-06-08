module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module fcl.p2p.relay;

import fcl.crypto.asymmetric;
import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;

export namespace fcl::p2p {

struct relay {
   enum class status : std::uint16_t {
      unused = 0,
      ok = 100,
      reservation_refused = 200,
      resource_limit_exceeded = 201,
      permission_denied = 202,
      connection_failed = 203,
      no_reservation = 204,
      malformed_message = 400,
      unexpected_message = 401,
   };

   struct limits {
      std::size_t max_active_relays = 128;
      std::size_t max_reservations = 1024;
      std::size_t max_streams_per_reservation = 64;
      std::uint64_t max_relay_bytes = 256 * 1024 * 1024;
      std::size_t max_queued_bytes = 16 * 1024 * 1024;
      std::chrono::milliseconds max_duration{60'000};
      std::chrono::milliseconds reservation_ttl{60'000};
      bool require_reservation = true;
   };

   struct limit {
      std::chrono::seconds duration{0};
      std::uint64_t data = 0;
   };

   struct peer {
      peer_id id;
      std::vector<endpoint> endpoints;
   };

   struct reservation {
      struct options {
         std::chrono::milliseconds ttl{60'000};
         std::size_t max_streams = 64;
         std::uint64_t max_bytes = 256 * 1024 * 1024;
         std::size_t max_queued_bytes = 16 * 1024 * 1024;
      };

      struct info {
         peer_id relay_peer;
         std::uint64_t id = 0;
         std::chrono::seconds expires_at{};
         std::chrono::milliseconds ttl{0};
         std::size_t max_streams = 0;
         std::uint64_t max_bytes = 0;
         std::size_t max_queued_bytes = 0;
         std::vector<endpoint> relay_endpoints;
         std::optional<signed_envelope> voucher;
      };

      std::uint64_t expires_at = 0;
      std::vector<endpoint> relay_endpoints;
      std::optional<signed_envelope> voucher;
   };

   struct voucher {
      peer_id relay_peer;
      peer_id peer;
      std::uint64_t expires_at = 0;
   };

   struct candidate {
      peer_id peer;
      double score = 0.0;
      bool has_reservation = false;
   };

   struct policy {
      bool service_enabled = false;
      bool client_enabled = true;
      bool public_relay_allowed = false;
      bool auto_discovery_enabled = true;
      std::size_t target_reservations = 2;
      std::chrono::milliseconds refresh_margin{15'000};
      std::size_t max_candidates_per_refresh = 20;
      std::size_t max_parallel_reservations = 2;
      std::chrono::milliseconds candidate_backoff{3'600'000};
   };

   struct hop_message {
      enum class message_kind : std::uint16_t {
         reserve = 0,
         connect = 1,
         status = 2,
      };

      message_kind kind = message_kind::reserve;
      std::optional<peer> target;
      std::optional<reservation> reservation_value;
      std::optional<limit> limit_value;
      status status = status::unused;
   };

   struct stop_message {
      enum class message_kind : std::uint16_t {
         connect = 0,
         status = 1,
      };

      message_kind kind = message_kind::connect;
      std::optional<peer> source;
      std::optional<limit> limit_value;
      status status = status::unused;
   };

   struct codec {
      [[nodiscard]] static std::vector<std::uint8_t> encode_hop(const hop_message& value);
      [[nodiscard]] static hop_message decode_hop(std::span<const std::uint8_t> bytes,
                                                  std::size_t max_message_size = 4 * 1024);

      [[nodiscard]] static std::vector<std::uint8_t> encode_stop(const stop_message& value);
      [[nodiscard]] static stop_message decode_stop(std::span<const std::uint8_t> bytes,
                                                    std::size_t max_message_size = 4 * 1024);

      [[nodiscard]] static std::vector<std::uint8_t> reservation_voucher_payload_type();
      [[nodiscard]] static signed_envelope seal_reservation_voucher(const voucher& value, const public_key& key,
                                                                    const fcl::crypto::asymmetric::private_key& private_key);
      [[nodiscard]] static voucher open_reservation_voucher(const signed_envelope& envelope, const peer_id& relay_peer,
                                                            std::uint64_t now_unix_seconds);
   };
};

} // namespace fcl::p2p
