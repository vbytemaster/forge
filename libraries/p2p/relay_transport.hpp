#pragma once

namespace fcl::p2p {

void trace_relay(std::string_view message);
[[nodiscard]] fcl::crypto::asymmetric::private_key private_key_from_pem(std::string_view pem);

class relay_secure_io;

struct yamux_frame {
   enum class type : std::uint8_t {
      data = 0,
      window_update = 1,
      ping = 2,
      go_away = 3,
   };

   type kind = type::data;
   std::uint16_t flags = 0;
   std::uint32_t stream_id = 0;
   std::vector<std::uint8_t> payload;
   std::uint32_t length_value = 0;
};

inline constexpr std::uint16_t yamux_syn = 0x01;
inline constexpr std::uint16_t yamux_ack = 0x02;
inline constexpr std::uint16_t yamux_fin = 0x04;
inline constexpr std::uint16_t yamux_rst = 0x08;
inline constexpr std::uint32_t yamux_initial_window = 256 * 1024;

class yamux_session : public std::enable_shared_from_this<yamux_session> {
 public:
   yamux_session(std::shared_ptr<relay_secure_io> secure, bool initiator);

   boost::asio::awaitable<fcl::p2p::stream> async_open_stream();
   boost::asio::awaitable<fcl::p2p::stream> async_accept_stream();
   [[nodiscard]] bool has_pending_stream() const noexcept;
   boost::asio::awaitable<void> async_close();
   boost::asio::awaitable<void> write_data(std::uint32_t stream_id, std::span<const std::uint8_t> bytes,
                                           std::uint16_t flags = 0);
   boost::asio::awaitable<std::vector<std::uint8_t>> read_data(std::uint32_t stream_id);
   boost::asio::awaitable<void> close_stream(std::uint32_t stream_id);

 private:
   boost::asio::awaitable<void> accept_remote_stream(std::uint32_t stream_id);
   class yamux_stream_backend;
   boost::asio::awaitable<void> write_frame(const yamux_frame& frame);
   boost::asio::awaitable<yamux_frame> read_frame();

   std::shared_ptr<relay_secure_io> secure_;
   std::uint32_t next_stream_id_ = 1;
   std::vector<std::uint8_t> buffer_;
   std::map<std::uint32_t, std::vector<std::vector<std::uint8_t>>> pending_;
   std::vector<std::uint32_t> pending_streams_;
};

boost::asio::awaitable<std::shared_ptr<yamux_session>>
upgrade_relay_outbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

boost::asio::awaitable<std::shared_ptr<yamux_session>>
upgrade_relay_inbound_session(fcl::p2p::stream stream, const node::options& options, const peer_id& expected_peer);

} // namespace fcl::p2p
