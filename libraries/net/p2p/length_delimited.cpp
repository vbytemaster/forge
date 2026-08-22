module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.net.p2p.node;

import forge.multiformats.exceptions;
import forge.multiformats.varint;
import forge.net.p2p.exceptions;
import forge.net.p2p.stream;

#include "details/length_delimited.hxx"

namespace forge::net::p2p {

boost::asio::awaitable<std::vector<std::uint8_t>> async_read_length_delimited(forge::net::p2p::stream& stream,
                                                                              std::vector<std::uint8_t>& buffer,
                                                                              std::size_t max_payload_size) {
   while (true) {
      try {
         const auto decoded = forge::multiformats::varint_decode(buffer);
         if (decoded.value > max_payload_size) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, "libp2p protobuf message exceeds max size");
         }
         const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
         if (buffer.size() >= total) {
            auto out = std::vector<std::uint8_t>{buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total)};
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
            co_return out;
         }
      } catch (const forge::multiformats::exceptions::invalid_format& error) {
         if (std::string_view{error.what()}.find("unterminated") == std::string_view::npos) {
            FORGE_THROW_EXCEPTION(exceptions::codec_error, error.what());
         }
      }
      auto chunk = co_await stream.async_read();
      buffer.insert(buffer.end(), chunk.begin(), chunk.end());
   }
}

} // namespace forge::net::p2p
