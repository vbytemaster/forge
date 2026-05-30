module;

#include <fcl/exception/macros.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.stream;

import fcl.p2p.exceptions;
import fcl.transport.frame;

namespace fcl::p2p {

struct stream::impl {
   fcl::transport::stream transport;
   std::vector<std::uint8_t> buffer;
};

stream::stream() = default;

stream::stream(fcl::transport::stream value) : impl_(std::make_shared<impl>()) {
   impl_->transport = std::move(value);
}

stream::stream(fcl::transport::stream value, std::vector<std::uint8_t> buffered) : stream{std::move(value)} {
   impl_->buffer = std::move(buffered);
}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

bool stream::valid() const noexcept {
   return impl_ && impl_->transport.valid();
}

std::int64_t stream::id() const noexcept {
   return impl_ ? impl_->transport.id() : -1;
}

boost::asio::awaitable<void> stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   co_await impl_->transport.async_write(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   if (!impl_->buffer.empty()) {
      auto out = std::move(impl_->buffer);
      impl_->buffer.clear();
      co_return out;
   }
   co_return co_await impl_->transport.async_read();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   co_await impl_->transport.async_write_frame(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   if (!impl_) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   if (impl_->buffer.empty()) {
      co_return co_await impl_->transport.async_read_frame();
   }
   while (true) {
      const auto decoded = fcl::transport::decode_frame(impl_->buffer);
      if (decoded.status == fcl::transport::frame_decode_status::complete) {
         auto payload = decoded.payload;
         impl_->buffer.erase(impl_->buffer.begin(), impl_->buffer.begin() + static_cast<std::ptrdiff_t>(decoded.consumed));
         co_return payload;
      }
      auto chunk = co_await impl_->transport.async_read();
      impl_->buffer.insert(impl_->buffer.end(), chunk.begin(), chunk.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_) {
      co_return;
   }
   co_await impl_->transport.async_close();
}

stream detail::stream_access::with_buffer(stream value, std::vector<std::uint8_t> buffered) {
   if (!value.impl_ || buffered.empty()) {
      return value;
   }
   if (!value.impl_->buffer.empty()) {
      value.impl_->buffer.insert(value.impl_->buffer.end(), buffered.begin(), buffered.end());
      return value;
   }
   value.impl_->buffer = std::move(buffered);
   return value;
}

} // namespace fcl::p2p
