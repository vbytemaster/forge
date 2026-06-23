module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module forge.p2p.rendezvous;

import forge.p2p.endpoint;
import forge.p2p.envelope;
import forge.p2p.identity;
import forge.crypto.asymmetric;

export namespace forge::p2p {

struct rendezvous {
   enum class role : std::uint16_t {
      client = 0,
      server = 1,
      client_and_server = 2,
   };

   enum class message_type : std::uint16_t {
      register_peer = 0,
      register_response = 1,
      unregister_peer = 2,
      discover = 3,
      discover_response = 4,
   };

   enum class status : std::uint16_t {
      ok = 0,
      invalid_namespace = 100,
      invalid_signed_peer_record = 101,
      invalid_ttl = 102,
      invalid_cookie = 103,
      not_authorized = 200,
      internal_error = 300,
      unavailable = 400,
   };

   struct options {
      role operating_role = role::client;
      std::chrono::seconds default_ttl{7'200};
      std::chrono::seconds min_ttl{7'200};
      std::chrono::seconds max_ttl{259'200};
      std::size_t max_namespace_size = 255;
      std::size_t max_registrations_per_peer = 1000;
      std::size_t max_discover_limit = 1000;
      std::size_t max_message_size = 1024 * 1024;
      bool require_signed_peer_record = true;
   };

   struct registration {
      std::string namespace_name;
      peer_id peer;
      std::vector<endpoint> endpoints;
      std::vector<std::uint8_t> signed_peer_record;
      std::chrono::seconds ttl{0};
      std::chrono::system_clock::time_point expires_at{};
      std::uint64_t sequence = 0;
   };

   struct peer_record {
      peer_id peer;
      std::vector<endpoint> endpoints;
      std::uint64_t sequence = 0;
   };

   struct register_request {
      std::string namespace_name;
      std::vector<std::uint8_t> signed_peer_record;
      std::chrono::seconds ttl{0};
   };

   struct register_response {
      status status_value = status::internal_error;
      std::string status_text;
      std::chrono::seconds ttl{0};
   };

   struct unregister_request {
      std::string namespace_name;
      std::optional<peer_id> peer;
   };

   struct discover_request {
      std::string namespace_name;
      std::size_t limit = 0;
      std::vector<std::uint8_t> cookie;
   };

   struct discover_response {
      std::vector<registration> registrations;
      std::vector<std::uint8_t> cookie;
      status status_value = status::ok;
      std::string status_text;
   };

   struct message {
      message_type type = message_type::discover;
      std::optional<register_request> register_value;
      std::optional<register_response> register_response_value;
      std::optional<unregister_request> unregister_value;
      std::optional<discover_request> discover_value;
      std::optional<discover_response> discover_response_value;
   };

   struct codec {
      [[nodiscard]] static std::vector<std::uint8_t> encode(const message& value);
      [[nodiscard]] static std::vector<std::uint8_t> encode(const message& value, const options& opts);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes);
      [[nodiscard]] static message decode(std::span<const std::uint8_t> bytes, const options& opts);
      [[nodiscard]] static std::vector<std::uint8_t> make_cookie(std::uint64_t sequence);
      [[nodiscard]] static std::vector<std::uint8_t> make_cookie(std::uint64_t sequence,
                                                                  std::string_view namespace_name);
      [[nodiscard]] static std::uint64_t read_cookie(std::span<const std::uint8_t> cookie);
      [[nodiscard]] static std::string read_cookie_namespace(std::span<const std::uint8_t> cookie);

      [[nodiscard]] static std::vector<std::uint8_t> encode_peer_record(const peer_record& value);
      [[nodiscard]] static peer_record decode_peer_record(std::span<const std::uint8_t> bytes);
      [[nodiscard]] static std::vector<std::uint8_t> peer_record_payload_type();
      [[nodiscard]] static signed_envelope seal_peer_record(const peer_record& value, const public_key& key,
                                                            const forge::crypto::asymmetric::private_key& private_key);
      [[nodiscard]] static peer_record open_peer_record(const signed_envelope& envelope,
                                                        std::optional<peer_id> expected_signer = std::nullopt);
   };
};

} // namespace forge::p2p
