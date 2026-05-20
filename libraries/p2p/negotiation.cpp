module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.negotiation;

import fcl.multiformats.varint;
import fcl.multiformats.types;
import fcl.p2p.errors;

namespace fcl::p2p::protocol_negotiation {
namespace {

[[nodiscard]] std::vector<std::uint8_t> bytes_for(std::string_view value) {
   return {value.begin(), value.end()};
}

[[nodiscard]] std::string string_for(std::span<const std::uint8_t> bytes) {
   return {bytes.begin(), bytes.end()};
}

void require_protocol_id(const protocol_id& protocol) {
   if (protocol.value.empty() || protocol.value.front() != '/') {
      throw_p2p_error(error_kind::protocol_error, "invalid multistream protocol id");
   }
}

[[nodiscard]] bool same_protocol(const protocol_id& left, const protocol_id& right) noexcept {
   return left.value == right.value;
}

class stream_io {
 public:
   stream_io(fcl::quic::stream stream_value, options opts_value)
       : stream_(std::move(stream_value)), opts_(opts_value) {}

   boost::asio::awaitable<void> write(message value) {
      const auto payload = encode_message(value, opts_);
      const auto frame = encode_frame(payload, opts_);
      co_await stream_.async_write(frame);
   }

   boost::asio::awaitable<message> read() {
      while (true) {
         try {
            auto frame = decode_frame(buffer_, opts_);
            auto payload = std::move(frame.payload);
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame.consumed));
            co_return decode_message(payload, opts_);
         } catch (const p2p_error& error) {
            if (error.kind() != error_kind::closed) {
               throw;
            }
         }
         auto chunk = co_await stream_.async_read();
         if (chunk.empty()) {
            throw_p2p_error(error_kind::closed, "multistream-select stream closed");
         }
         buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
      }
   }

   [[nodiscard]] fcl::p2p::stream finish() {
      return fcl::p2p::stream{std::move(stream_), std::move(buffer_)};
   }

 private:
   fcl::quic::stream stream_;
   options opts_;
   std::vector<std::uint8_t> buffer_;
};

} // namespace

std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload, options opts) {
   if (payload.size() > opts.max_frame_size) {
      throw_p2p_error(error_kind::codec_error, "multistream-select frame is too large");
   }
   auto out = fcl::multiformats::varint_encode(payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
   return out;
}

decoded_frame decode_frame(std::span<const std::uint8_t> bytes, options opts) {
   auto decoded = fcl::multiformats::decoded_varint{};
   try {
      decoded = fcl::multiformats::varint_decode(bytes);
   } catch (const fcl::multiformats::format_error& error) {
      if (bytes.size() < 2) {
         throw_p2p_error(error_kind::closed, "multistream-select frame needs more bytes");
      }
      throw_p2p_error(error_kind::codec_error, error.what());
   }
   if (decoded.value > opts.max_frame_size) {
      throw_p2p_error(error_kind::codec_error, "multistream-select frame is too large");
   }
   const auto total = decoded.size + static_cast<std::size_t>(decoded.value);
   if (bytes.size() < total) {
      throw_p2p_error(error_kind::closed, "multistream-select frame needs more bytes");
   }
   return decoded_frame{
       .payload = std::vector<std::uint8_t>{bytes.begin() + static_cast<std::ptrdiff_t>(decoded.size),
                                            bytes.begin() + static_cast<std::ptrdiff_t>(total)},
       .consumed = total,
   };
}

std::vector<std::uint8_t> encode_message(const message& value, options opts) {
   auto out = std::vector<std::uint8_t>{};
   switch (value.kind) {
   case message_kind::header:
      out = bytes_for(multistream_v1.value);
      out.push_back('\n');
      return out;
   case message_kind::protocol:
      require_protocol_id(value.protocol);
      out = bytes_for(value.protocol.value);
      out.push_back('\n');
      return out;
   case message_kind::list_protocols:
      out = bytes_for(list_protocols.value);
      out.push_back('\n');
      return out;
   case message_kind::not_available:
      out = bytes_for(not_available.value);
      out.push_back('\n');
      return out;
   case message_kind::protocols:
      if (value.protocols.size() > opts.max_protocols) {
         throw_p2p_error(error_kind::codec_error, "too many multistream-select protocols");
      }
      for (const auto& protocol : value.protocols) {
         require_protocol_id(protocol);
         auto line = bytes_for(protocol.value);
         line.push_back('\n');
         auto encoded_length = fcl::multiformats::varint_encode(line.size());
         out.insert(out.end(), encoded_length.begin(), encoded_length.end());
         out.insert(out.end(), line.begin(), line.end());
      }
      out.push_back('\n');
      return out;
   }
   throw_p2p_error(error_kind::internal_error, "unknown multistream-select message kind");
}

message decode_message(std::span<const std::uint8_t> bytes, options opts) {
   const auto text = string_for(bytes);
   if (text == multistream_v1.value + "\n") {
      return message{.kind = message_kind::header, .protocol = multistream_v1};
   }
   if (text == not_available.value + "\n") {
      return message{.kind = message_kind::not_available, .protocol = not_available};
   }
   if (text == list_protocols.value + "\n") {
      return message{.kind = message_kind::list_protocols, .protocol = list_protocols};
   }
   if (!text.empty() && text.front() == '/' && text.back() == '\n' &&
       text.find('\n') == text.size() - 1) {
      return message{.kind = message_kind::protocol, .protocol = protocol_id{.value = text.substr(0, text.size() - 1)}};
   }

   auto protocols = std::vector<protocol_id>{};
   auto offset = std::size_t{0};
   while (offset < bytes.size()) {
      if (bytes[offset] == '\n' && offset + 1 == bytes.size()) {
         return message{.kind = message_kind::protocols, .protocols = std::move(protocols)};
      }
      if (protocols.size() >= opts.max_protocols) {
         throw_p2p_error(error_kind::codec_error, "too many multistream-select protocols");
      }
      auto decoded = fcl::multiformats::decoded_varint{};
      try {
         decoded = fcl::multiformats::varint_decode(bytes.subspan(offset));
      } catch (const fcl::multiformats::format_error& error) {
         throw_p2p_error(error_kind::codec_error, error.what());
      }
      if (decoded.value == 0 || offset + decoded.size + decoded.value > bytes.size()) {
         throw_p2p_error(error_kind::codec_error, "invalid multistream-select protocols list");
      }
      const auto begin = offset + decoded.size;
      const auto end = begin + static_cast<std::size_t>(decoded.value);
      if (bytes[end - 1] != '\n') {
         throw_p2p_error(error_kind::codec_error, "multistream-select protocol is not newline-terminated");
      }
      const auto protocol = string_for(bytes.subspan(begin, static_cast<std::size_t>(decoded.value) - 1));
      if (protocol.empty() || protocol.front() != '/') {
         throw_p2p_error(error_kind::protocol_error, "invalid multistream-select protocol id");
      }
      protocols.push_back(protocol_id{.value = protocol});
      offset = end;
   }

   throw_p2p_error(error_kind::codec_error, "invalid multistream-select message");
}

boost::asio::awaitable<fcl::p2p::stream> async_select(fcl::quic::stream raw, protocol_id protocol, options opts) {
   require_protocol_id(protocol);
   auto io = stream_io{std::move(raw), opts};
   co_await io.write(message{.kind = message_kind::header, .protocol = multistream_v1});
   co_await io.write(message{.kind = message_kind::protocol, .protocol = protocol});

   auto header = co_await io.read();
   if (header.kind != message_kind::header) {
      throw_p2p_error(error_kind::protocol_error, "multistream-select expected header response");
   }
   auto selected = co_await io.read();
   if (selected.kind == message_kind::not_available) {
      throw_p2p_error(error_kind::unsupported_protocol, "remote peer does not support requested protocol");
   }
   if (selected.kind != message_kind::protocol || !same_protocol(selected.protocol, protocol)) {
      throw_p2p_error(error_kind::protocol_error, "multistream-select selected unexpected protocol");
   }
   co_return io.finish();
}

boost::asio::awaitable<negotiated_stream> async_accept(fcl::quic::stream raw, std::vector<protocol_id> protocols,
                                                       options opts) {
   if (protocols.size() > opts.max_protocols) {
      throw_p2p_error(error_kind::codec_error, "too many local multistream-select protocols");
   }
   for (const auto& protocol : protocols) {
      require_protocol_id(protocol);
   }

   auto io = stream_io{std::move(raw), opts};
   auto header = co_await io.read();
   if (header.kind != message_kind::header) {
      throw_p2p_error(error_kind::protocol_error, "multistream-select expected header");
   }
   co_await io.write(message{.kind = message_kind::header, .protocol = multistream_v1});

   while (true) {
      auto request = co_await io.read();
      if (request.kind == message_kind::list_protocols) {
         co_await io.write(message{.kind = message_kind::protocols, .protocols = protocols});
         continue;
      }
      if (request.kind != message_kind::protocol) {
         throw_p2p_error(error_kind::protocol_error, "multistream-select expected protocol proposal");
      }
      const auto found = std::find_if(protocols.begin(), protocols.end(), [&](const protocol_id& candidate) {
         return same_protocol(candidate, request.protocol);
      });
      if (found == protocols.end()) {
         co_await io.write(message{.kind = message_kind::not_available, .protocol = not_available});
         continue;
      }
      co_await io.write(message{.kind = message_kind::protocol, .protocol = *found});
      co_return negotiated_stream{.protocol = *found, .stream = io.finish()};
   }
}

} // namespace fcl::p2p::protocol_negotiation
