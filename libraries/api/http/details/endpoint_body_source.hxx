#pragma once

namespace forge::api::http::detail {

class endpoint_body_source final : public forge::net::http::body_reader::source {
 public:
   endpoint_body_source(std::shared_ptr<forge::api::core::detail::stream_endpoint> endpoint,
                        forge::api::core::stream_direction direction,
                        forge::api::core::api_ref api,
                        std::string method,
                        forge::api::core::codec_id codec,
                        std::uint32_t max_frame_bytes,
                        std::uint32_t max_item_bytes);

   boost::asio::awaitable<std::optional<forge::net::http::body_chunk>> async_read() override;
   [[nodiscard]] std::uint64_t bytes_read() const noexcept override;

 private:
   [[nodiscard]] forge::api::core::frame make_item(forge::api::core::bytes payload) const;
   [[nodiscard]] forge::api::core::frame make_end() const;

   std::shared_ptr<forge::api::core::detail::stream_endpoint> endpoint_;
   forge::api::core::stream_direction direction_;
   forge::api::core::api_ref api_;
   std::string method_;
   forge::api::core::codec_id codec_;
   std::uint32_t max_frame_bytes_ = 0;
   std::uint32_t max_item_bytes_ = 0;
   std::uint64_t bytes_read_ = 0;
   bool end_sent_ = false;
};

} // namespace forge::api::http::detail
