module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.transport.stream;

import fcl.transport.exceptions;

namespace fcl::transport {

struct stream::impl {
   std::shared_ptr<detail::stream_concept> model;
   std::vector<std::uint8_t> buffer;
};

stream::stream() = default;
stream::stream(std::shared_ptr<detail::stream_concept> model) : impl_(std::make_shared<impl>()) {
   impl_->model = std::move(model);
}

stream::~stream() = default;
stream::stream(stream&&) noexcept = default;
stream& stream::operator=(stream&&) noexcept = default;

bool stream::valid() const noexcept {
   return impl_ && impl_->model && impl_->model->valid();
}

std::int64_t stream::id() const noexcept {
   return impl_ && impl_->model ? impl_->model->id() : -1;
}

boost::asio::awaitable<void> stream::async_write(std::span<const std::uint8_t> bytes) {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   auto owned = std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
   co_await impl_->model->async_write(owned);
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   if (!impl_->buffer.empty()) {
      auto out = std::move(impl_->buffer);
      impl_->buffer.clear();
      co_return out;
   }
   co_return co_await impl_->model->async_read();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   co_await async_write(encode_frame(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   if (!valid()) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   while (true) {
      const auto decoded = decode_frame(impl_->buffer);
      if (decoded.status == frame_decode_status::complete) {
         auto payload = decoded.payload;
         impl_->buffer.erase(impl_->buffer.begin(), impl_->buffer.begin() + static_cast<std::ptrdiff_t>(decoded.consumed));
         co_return payload;
      }
      auto chunk = co_await impl_->model->async_read();
      impl_->buffer.insert(impl_->buffer.end(), chunk.begin(), chunk.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!valid()) {
      co_return;
   }
   co_await impl_->model->async_close();
}

stream detail::stream_access::make(std::shared_ptr<stream_concept> model) {
   return stream{std::move(model)};
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

} // namespace fcl::transport
