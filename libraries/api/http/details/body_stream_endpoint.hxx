#pragma once

namespace forge::api::http::detail {

class body_stream_endpoint final : public forge::api::core::detail::stream_endpoint {
 public:
   using decoder = std::function<void(const forge::api::core::bytes&, forge::raw::unpack_limits)>;

   body_stream_endpoint(forge::net::http::body_reader body,
                        forge::api::core::stream_direction direction,
                        decoder item_decoder,
                        std::uint32_t max_frame_bytes,
                        std::uint32_t max_item_bytes,
                        bool terminal_required);

   boost::asio::awaitable<std::optional<forge::api::core::bytes>> async_read() override;
   boost::asio::awaitable<void> async_write(forge::api::core::bytes value) override;
   void close() noexcept override;
   void fail(std::exception_ptr error) noexcept override;

   boost::asio::awaitable<forge::api::core::frame> async_finish();

 private:
   [[nodiscard]] forge::raw::unpack_limits payload_limits(std::size_t size) const noexcept;
   void validate_call_frame(const forge::api::core::frame& value) const;
   void remember_terminal(forge::api::core::frame value);
   void rethrow_failure() const;

   stream_frame_decoder decoder_;
   decoder item_decoder_;
   forge::api::core::stream_direction direction_;
   std::uint32_t max_item_bytes_ = 0;
   mutable std::mutex mutex_;
   std::optional<forge::api::core::frame> terminal_;
   std::exception_ptr failure_;
   std::atomic_bool ended_ = false;
   bool terminal_required_ = false;
};

} // namespace forge::api::http::detail
