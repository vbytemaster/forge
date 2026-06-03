module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>

export module fcl.transport.stream;

export import fcl.transport.frame;

export namespace fcl::transport {

namespace detail {
class stream_concept;
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
   boost::asio::awaitable<void> async_write_frame(std::span<const std::uint8_t> bytes);
   boost::asio::awaitable<std::vector<std::uint8_t>> async_read_frame();
   boost::asio::awaitable<void> async_close();
   void cancel();

 private:
   friend struct detail::stream_access;

   struct impl;
   explicit stream(std::shared_ptr<detail::stream_concept> model);

   std::shared_ptr<impl> impl_;
};

namespace detail {

class stream_concept {
 public:
   virtual ~stream_concept() = default;

   [[nodiscard]] virtual bool valid() const noexcept = 0;
   [[nodiscard]] virtual std::int64_t id() const noexcept = 0;

   virtual boost::asio::awaitable<void> async_write(std::span<const std::uint8_t> bytes) = 0;
   virtual boost::asio::awaitable<std::vector<std::uint8_t>> async_read() = 0;
   virtual boost::asio::awaitable<void> async_close() = 0;
   virtual void cancel() = 0;
};

struct stream_access {
   [[nodiscard]] static stream make(std::shared_ptr<stream_concept> model);
   [[nodiscard]] static stream with_buffer(stream value, std::vector<std::uint8_t> buffered);
};

} // namespace detail

} // namespace fcl::transport
