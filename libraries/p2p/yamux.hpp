#pragma once

namespace fcl::p2p::yamux {

struct options {
   std::uint32_t initial_window = 256 * 1024;
};

class session : public std::enable_shared_from_this<session> {
 public:
   session(fcl::transport::stream stream, bool initiator, options options_value = {});

   boost::asio::awaitable<fcl::transport::stream> async_open_stream();
   boost::asio::awaitable<fcl::transport::stream> async_accept_stream();
   [[nodiscard]] bool has_pending_stream() const noexcept;
   boost::asio::awaitable<void> async_close();
   boost::asio::awaitable<void> write_data(std::uint32_t stream_id, std::span<const std::uint8_t> bytes,
                                           std::uint16_t flags = 0);
   boost::asio::awaitable<std::vector<std::uint8_t>> read_data(std::uint32_t stream_id);
   boost::asio::awaitable<void> close_stream(std::uint32_t stream_id);

 private:
   struct frame;
   class stream_concept;

   boost::asio::awaitable<void> accept_remote_stream(std::uint32_t stream_id);
   boost::asio::awaitable<void> write_frame(const frame& value);
   boost::asio::awaitable<frame> read_frame();

   fcl::transport::stream stream_;
   options options_;
   std::uint32_t next_stream_id_ = 1;
   std::vector<std::uint8_t> buffer_;
   std::vector<std::uint32_t> pending_streams_;
   std::map<std::uint32_t, std::vector<std::vector<std::uint8_t>>> pending_payloads_;
};

} // namespace fcl::p2p::yamux
