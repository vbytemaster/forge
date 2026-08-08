#pragma once

namespace forge::api::http::detail {

class stream_frame_decoder {
 public:
   explicit stream_frame_decoder(forge::net::http::body_reader body,
                                 std::uint32_t max_frame_bytes);

   boost::asio::awaitable<std::optional<forge::api::core::frame>> async_read();
   void cancel() noexcept;

   [[nodiscard]] static forge::net::http::body_chunk
   encode(const forge::api::core::frame& value, std::uint32_t max_frame_bytes);

 private:
   void compact();

   forge::net::http::body_reader body_;
   std::vector<std::uint8_t> buffered_;
   std::size_t offset_ = 0;
   std::uint32_t max_frame_bytes_ = 0;
   bool eof_ = false;
};

} // namespace forge::api::http::detail
