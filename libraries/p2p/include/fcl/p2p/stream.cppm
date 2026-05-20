module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.p2p.stream;

import fcl.quic.framed_stream;
import fcl.quic.stream;

export namespace fcl::p2p {

class stream {
 public:
   stream();
   explicit stream(fcl::quic::stream value);
   stream(fcl::quic::stream value, std::vector<std::uint8_t> buffered);
   explicit stream(fcl::quic::framed_stream value);
   ~stream();

   stream(stream&&) noexcept;
   stream& operator=(stream&&) noexcept;

   stream(const stream&) = delete;
   stream& operator=(const stream&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] std::int64_t id() const noexcept;

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read();
   boost::asio::awaitable<void> async_write_frame(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read_frame();
   boost::asio::awaitable<void> async_close();

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace fcl::p2p
