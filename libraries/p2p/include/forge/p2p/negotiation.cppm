module;

#include <cstdint>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module forge.p2p.negotiation;

import forge.p2p.protocol;
import forge.p2p.stream;
import forge.transport.stream;

export namespace forge::p2p {

namespace protocol_negotiation {
   inline const protocol_id multistream_v1{.value = "/multistream/1.0.0"};
   inline const protocol_id not_available{.value = "na"};
   inline const protocol_id list_protocols{.value = "ls"};

   struct options {
      std::uint16_t max_frame_size = 16 * 1024 - 1;
      std::uint16_t max_protocols = 1000;
   };

   enum class message_kind {
      header,
      protocol,
      list_protocols,
      protocols,
      not_available,
   };

   struct message {
      message_kind kind = message_kind::header;
      protocol_id protocol;
      std::vector<protocol_id> protocols;
   };

   struct decoded_frame {
      std::vector<std::uint8_t> payload;
      std::size_t consumed = 0;
   };

   struct negotiated_stream {
      protocol_id protocol;
      forge::p2p::stream stream;
   };

   [[nodiscard]] std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload, options opts = {});
   [[nodiscard]] decoded_frame decode_frame(std::span<const std::uint8_t> bytes, options opts = {});
   [[nodiscard]] std::vector<std::uint8_t> encode_message(const message& value, options opts = {});
   [[nodiscard]] message decode_message(std::span<const std::uint8_t> bytes, options opts = {});

   boost::asio::awaitable<forge::p2p::stream> async_select(forge::transport::stream stream, protocol_id protocol,
                                                         options opts = {});
   boost::asio::awaitable<forge::p2p::stream> async_select(forge::p2p::stream stream, protocol_id protocol,
                                                         options opts = {});
   boost::asio::awaitable<negotiated_stream> async_accept(forge::transport::stream stream,
                                                          std::vector<protocol_id> protocols, options opts = {});
   boost::asio::awaitable<negotiated_stream> async_accept(forge::p2p::stream stream,
                                                          std::vector<protocol_id> protocols, options opts = {});
} // namespace protocol_negotiation

} // namespace forge::p2p
