module;

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.p2p.stream;

import fcl.p2p.exceptions;
import fcl.quic.exceptions;
import fcl.quic.framed_stream;

namespace fcl::p2p {

struct stream::impl {
   enum class mode { none, raw, framed, backend };

   mode kind = mode::none;
   fcl::quic::stream raw;
   fcl::quic::framed_stream framed{fcl::quic::stream{}};
   std::shared_ptr<detail::stream_backend> backend;
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

stream::stream(std::shared_ptr<detail::stream_backend> backend) : impl_(std::make_shared<impl>()) {
   impl_->kind = impl::mode::backend;
   impl_->backend = std::move(backend);
}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

bool stream::valid() const noexcept {
   if (!impl_) {
      return false;
   }
   if (impl_->kind == impl::mode::raw) {
      return impl_->raw.valid();
   }
   if (impl_->kind == impl::mode::framed) {
      return impl_->framed.valid();
   }
   return impl_->kind == impl::mode::backend && impl_->backend && impl_->backend->valid();
}

std::int64_t stream::id() const noexcept {
   if (!impl_) {
      return -1;
   }
   if (impl_->kind == impl::mode::raw) {
      return impl_->raw.id();
   }
   if (impl_->kind == impl::mode::framed) {
      return impl_->framed.id();
   }
   return impl_->backend ? impl_->backend->id() : -1;
}

boost::asio::awaitable<void> stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      exceptions::raise(exceptions::code::closed, "invalid P2P stream");
   }
   if (impl_->kind == impl::mode::backend) {
      co_await impl_->backend->async_write(bytes);
      co_return;
   }
   if (impl_->kind != impl::mode::raw) {
      exceptions::raise(exceptions::code::protocol_error, "raw write is not available on legacy framed P2P stream");
   }
   co_await impl_->raw.async_write(bytes);
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   if (!impl_) {
      exceptions::raise(exceptions::code::closed, "invalid P2P stream");
   }
   if (!impl_->buffer.empty()) {
      auto out = std::move(impl_->buffer);
      impl_->buffer.clear();
      co_return out;
   }
   if (impl_->kind == impl::mode::backend) {
      co_return co_await impl_->backend->async_read();
   }
   if (impl_->kind != impl::mode::raw) {
      exceptions::raise(exceptions::code::protocol_error, "raw read is not available on legacy framed P2P stream");
   }
   co_return co_await impl_->raw.async_read();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   if (!impl_) {
      exceptions::raise(exceptions::code::closed, "invalid P2P stream");
   }
   if (impl_->kind == impl::mode::framed) {
      co_await impl_->framed.async_write_frame(bytes);
      co_return;
   }
   if (impl_->kind == impl::mode::backend) {
      co_await impl_->backend->async_write(fcl::quic::encode_frame(bytes));
      co_return;
   }
   co_await impl_->raw.async_write(fcl::quic::encode_frame(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   if (!impl_) {
      exceptions::raise(exceptions::code::closed, "invalid P2P stream");
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
      auto chunk = std::vector<std::uint8_t>{};
      if (impl_->kind == impl::mode::backend) {
         chunk = co_await impl_->backend->async_read();
      } else {
         chunk = co_await impl_->raw.async_read();
      }
      impl_->buffer.insert(impl_->buffer.end(), chunk.begin(), chunk.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_) {
      co_return;
   }
   if (impl_->kind == impl::mode::backend) {
      co_await impl_->backend->async_close();
   } else if (impl_->kind == impl::mode::raw) {
      co_await impl_->raw.async_close();
   } else {
      co_await impl_->framed.async_close();
   }
}

stream detail::stream_access::make(std::shared_ptr<stream_backend> backend) {
   return stream{std::move(backend)};
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
