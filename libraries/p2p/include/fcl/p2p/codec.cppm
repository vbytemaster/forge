module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.p2p.codec;

import fcl.p2p.message;
import fcl.quic.framed_stream;

export namespace fcl::p2p {

namespace control_codec {
   struct options {
      std::uint32_t max_message_size = 4 * 1024 * 1024;
      std::uint32_t max_endpoint_records = 1024;
   };

   [[nodiscard]] std::vector<std::uint8_t> encode(const control_message& message, options opts = {});
   [[nodiscard]] control_message decode(std::span<const std::uint8_t> bytes, options opts = {});

   boost::asio::awaitable<void> async_write(fcl::quic::framed_stream& stream, const control_message& message,
                                            options opts = {});

   boost::asio::awaitable<control_message> async_read(fcl::quic::framed_stream& stream, options opts = {});
} // namespace control_codec

} // namespace fcl::p2p
