#pragma once

namespace forge::net::p2p {

struct peer_exchange_message {
   enum class type : std::uint16_t {
      hello = 1,
      peer_exchange_request = 6,
      peer_exchange_response = 7,
      ping = 12,
      pong = 13,
      goaway = 14
   };

   struct endpoint_record {
      peer_id peer;
      forge::net::p2p::endpoint endpoint;
      capability_set capabilities{};
   };

   type kind = type::ping;
   std::uint64_t request_id = 0;
   std::uint32_t flags = 0;
   peer_id peer;
   protocol_id protocol;
   capability_set capabilities{};
   std::uint64_t max_frame_size = 4 * 1024 * 1024;
   std::string reason;
   std::vector<endpoint_record> endpoints;
   std::vector<std::uint8_t> payload;
};

namespace peer_exchange_codec {

struct options {
   std::uint32_t max_message_size = 4 * 1024 * 1024;
   std::uint32_t max_endpoint_records = 1024;
};

inline constexpr std::uint32_t minimum_message_size = 56;

[[nodiscard]] std::size_t encoded_size(const peer_exchange_message& message, options opts = {});
[[nodiscard]] std::uint32_t negotiate_response_max_frame_size(std::uint64_t offered, options local);
[[nodiscard]] std::vector<std::uint8_t> encode(const peer_exchange_message& message, options opts = {});
[[nodiscard]] peer_exchange_message decode(std::span<const std::uint8_t> bytes, options opts = {});
boost::asio::awaitable<void> async_write(forge::net::p2p::stream& stream, const peer_exchange_message& message,
                                         options opts = {});
boost::asio::awaitable<peer_exchange_message> async_read(forge::net::p2p::stream& stream, options opts = {});

} // namespace peer_exchange_codec

} // namespace forge::net::p2p
