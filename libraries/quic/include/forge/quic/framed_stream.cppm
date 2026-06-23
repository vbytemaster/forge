module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module forge.quic.framed_stream;

import forge.quic.exceptions;
import forge.quic.stream;

export namespace forge::quic {

struct frame_codec_options {
   std::uint32_t max_frame_size = 16 * 1024 * 1024;
};

enum class frame_decode_status { complete, need_more_data };

struct frame_decode_result {
   frame_decode_status status = frame_decode_status::need_more_data;
   std::vector<std::uint8_t> payload;
   std::size_t consumed = 0;
};

[[nodiscard]] std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload,
                                                     frame_codec_options options = {});
[[nodiscard]] frame_decode_result decode_frame(std::span<const std::uint8_t> bytes, frame_codec_options options = {});

class framed_stream {
 public:
   explicit framed_stream(stream stream_value, frame_codec_options options = {});

   framed_stream(framed_stream&&) noexcept;
   framed_stream& operator=(framed_stream&&) noexcept;

   framed_stream(const framed_stream&) = delete;
   framed_stream& operator=(const framed_stream&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] std::int64_t id() const noexcept;

   boost::asio::awaitable<void> async_write_frame(std::span<const std::uint8_t> payload);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read_frame();
   boost::asio::awaitable<void> async_close();

 private:
   stream stream_;
   frame_codec_options options_;
   std::vector<std::uint8_t> buffer_;
};

} // namespace forge::quic
