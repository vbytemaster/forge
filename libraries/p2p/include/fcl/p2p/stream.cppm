module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.p2p.stream;

import fcl.transport.stream;

export namespace fcl::p2p {

namespace detail {
struct stream_access;
} // namespace detail

class stream {
 public:
   stream();
   explicit stream(fcl::transport::stream value);
   stream(fcl::transport::stream value, std::vector<std::uint8_t> buffered);
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
   void cancel();

 private:
   friend struct detail::stream_access;

   struct impl;

   std::shared_ptr<impl> impl_;
};

namespace detail {

struct stream_access {
   [[nodiscard]] static stream with_buffer(stream value, std::vector<std::uint8_t> buffered);
};

} // namespace detail

} // namespace fcl::p2p
