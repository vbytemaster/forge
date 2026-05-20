module;

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.stream;

import fcl.p2p.errors;
import fcl.quic.errors;
import fcl.quic.framed_stream;

namespace fcl::p2p {

struct stream::impl {
   enum class mode { none, raw, framed };

   mode kind = mode::none;
   fcl::quic::stream raw;
   fcl::quic::framed_stream framed{fcl::quic::stream{}};
   std::vector<std::uint8_t> buffer;
};

stream::stream() = default;

stream::stream(fcl::quic::stream value) : impl_(std::make_shared<impl>()) {
   impl_->kind = impl::mode::raw;
   impl_->raw = std::move(value);
}

stream::stream(fcl::quic::stream value, std::vector<std::uint8_t> buffered) : stream{std::move(value)} {
   impl_->buffer = std::move(buffered);
}

stream::stream(fcl::quic::framed_stream value) : impl_(std::make_shared<impl>()) {
   impl_->kind = impl::mode::framed;
   impl_->framed = std::move(value);
}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

bool stream::valid() const noexcept {
   if (!impl_) {
      return false;
   }
   return impl_->kind == impl::mode::raw ? impl_->raw.valid() : impl_->framed.valid();
}

std::int64_t stream::id() const noexcept {
   if (!impl_) {
      return -1;
   }
   return impl_->kind == impl::mode::raw ? impl_->raw.id() : impl_->framed.id();
}

boost::asio::awaitable<void> stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      throw_p2p_error(error_kind::closed, "invalid P2P stream");
   }
   if (impl_->kind != impl::mode::raw) {
      throw_p2p_error(error_kind::protocol_error, "raw write is not available on legacy framed P2P stream");
   }
   co_await impl_->raw.async_write(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   if (!impl_) {
      throw_p2p_error(error_kind::closed, "invalid P2P stream");
   }
   if (impl_->kind != impl::mode::raw) {
      throw_p2p_error(error_kind::protocol_error, "raw read is not available on legacy framed P2P stream");
   }
   if (!impl_->buffer.empty()) {
      auto out = std::move(impl_->buffer);
      impl_->buffer.clear();
      co_return out;
   }
   co_return co_await impl_->raw.async_read();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      throw_p2p_error(error_kind::closed, "invalid P2P stream");
   }
   if (impl_->kind == impl::mode::framed) {
      co_await impl_->framed.async_write_frame(bytes);
      co_return;
   }
   co_await impl_->raw.async_write(fcl::quic::encode_frame(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   if (!impl_) {
      throw_p2p_error(error_kind::closed, "invalid P2P stream");
   }
   if (impl_->kind == impl::mode::framed) {
      co_return co_await impl_->framed.async_read_frame();
   }
   while (true) {
      const auto decoded = fcl::quic::decode_frame(impl_->buffer);
      if (decoded.status == fcl::quic::frame_decode_status::complete) {
         auto payload = decoded.payload;
         impl_->buffer.erase(impl_->buffer.begin(), impl_->buffer.begin() + static_cast<std::ptrdiff_t>(decoded.consumed));
         co_return payload;
      }
      auto chunk = co_await impl_->raw.async_read();
      impl_->buffer.insert(impl_->buffer.end(), chunk.begin(), chunk.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_) {
      co_return;
   }
   if (impl_->kind == impl::mode::raw) {
      co_await impl_->raw.async_close();
   } else {
      co_await impl_->framed.async_close();
   }
}

} // namespace fcl::p2p
