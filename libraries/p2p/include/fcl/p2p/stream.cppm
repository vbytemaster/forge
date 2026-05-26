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

namespace detail {
class stream_backend;
struct stream_access;
} // namespace detail

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
   friend struct detail::stream_access;

   struct impl;
   explicit stream(std::shared_ptr<detail::stream_backend> backend);

   std::shared_ptr<impl> impl_;
};

namespace detail {

class stream_backend {
 public:
   virtual ~stream_backend() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   [[nodiscard]] virtual std::int64_t id() const noexcept = 0;

   virtual boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) = 0;
   virtual boost::asio::awaitable<std::vector<std::uint8_t>> async_read() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
};

struct stream_access {
   [[nodiscard]] static stream make(std::shared_ptr<stream_backend> backend);
   [[nodiscard]] static stream with_buffer(stream value, std::vector<std::uint8_t> buffered);
};

} // namespace detail

} // namespace fcl::p2p
