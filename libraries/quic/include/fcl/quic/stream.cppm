module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.quic.stream;

export namespace fcl::quic {

namespace detail {
struct stream_handle;
struct stream_access;
} // namespace detail

class stream {
 public:
   stream();
   ~stream();

   stream(stream&&) noexcept;
   stream& operator=(stream&&) noexcept;

   stream(const stream&) = delete;
   stream& operator=(const stream&) = delete;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] std::int64_t id() const noexcept;

   boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::stream_access;

   explicit stream(detail::stream_handle handle);

   struct impl;
   std::shared_ptr<impl> impl_;
};

namespace detail {

struct stream_access {
   static stream make(stream_handle handle);
};

} // namespace detail

} // namespace fcl::quic
