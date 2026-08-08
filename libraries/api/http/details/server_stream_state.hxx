#pragma once

namespace forge::api::http::detail {

class server_stream_state final : public std::enable_shared_from_this<server_stream_state> {
 public:
   server_stream_state(boost::asio::any_io_executor executor,
                       forge::api::core::binding_plan plan,
                       forge::api::core::frame request,
                       std::uint32_t max_frame_bytes,
                       std::uint32_t max_item_bytes,
                       std::uint32_t max_buffered_items,
                       std::uint64_t max_buffered_bytes);

   void start();
   void cancel() noexcept;
   boost::asio::awaitable<std::optional<forge::net::http::body_chunk>> async_next();

 private:
   boost::asio::awaitable<void> run();
   boost::asio::awaitable<forge::api::core::frame> wait_terminal();
   void publish_terminal(forge::api::core::frame value) noexcept;
   [[nodiscard]] forge::api::core::frame internal_error() const;

   boost::asio::any_io_executor executor_;
   boost::asio::cancellation_signal cancellation_;
   forge::api::core::binding_plan plan_;
   forge::api::core::frame request_;
   forge::api::core::detail::local_stream_pair stream_;
   std::uint32_t max_frame_bytes_ = 0;
   std::mutex mutex_;
   std::optional<forge::api::core::frame> terminal_;
   std::shared_ptr<boost::asio::steady_timer> terminal_ready_;
   bool stream_end_sent_ = false;
   bool terminal_sent_ = false;
   std::atomic_bool cancelled_ = false;
};

} // namespace forge::api::http::detail
