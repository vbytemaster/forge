module;

#include <fcl/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module fcl.transport.stream;

import fcl.transport.exceptions;

namespace fcl::transport {
namespace {

constexpr auto compact_threshold = std::size_t{65'536};

void compact_buffer(std::vector<std::uint8_t>& buffer, std::size_t& consumed) {
   if (consumed == 0) {
      return;
   }
   if (consumed >= buffer.size()) {
      buffer.clear();
      consumed = 0;
      return;
   }
   auto compacted = std::vector<std::uint8_t>{};
   compacted.reserve(buffer.size() - consumed);
   compacted.insert(compacted.end(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed), buffer.end());
   buffer = std::move(compacted);
   consumed = 0;
}

[[nodiscard]] std::span<const std::uint8_t> available_bytes(const std::vector<std::uint8_t>& buffer,
                                                            std::size_t consumed) noexcept {
   if (consumed >= buffer.size()) {
      return {};
   }
   return {buffer.data() + consumed, buffer.size() - consumed};
}

} // namespace

struct stream::impl {
   std::shared_ptr<detail::stream_concept> model;
   std::vector<std::uint8_t> buffer;
   std::size_t consumed = 0;
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
   auto owned = chunk{bytes};
   co_await async_write(std::move(owned));
}

boost::asio::awaitable<void> stream::async_write(chunk bytes) {
   if (!impl_ || !impl_->model) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   co_await impl_->model->async_write_chunk(std::move(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   auto value = co_await async_read_chunk();
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<chunk> stream::async_read_chunk() {
   if (!impl_ || !impl_->model) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   if (!available_bytes(impl_->buffer, impl_->consumed).empty()) {
      auto view = available_bytes(impl_->buffer, impl_->consumed);
      auto out = chunk{view};
      impl_->buffer.clear();
      impl_->consumed = 0;
      co_return out;
   }
   co_return co_await impl_->model->async_read_chunk();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   auto encoded = encode_frame(bytes);
   co_await async_write(chunk{std::move(encoded)});
}

boost::asio::awaitable<void> stream::async_write_frame(chunk bytes) {
   co_await async_write_frame(bytes.bytes());
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   auto value = co_await async_read_frame_chunk();
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<chunk> stream::async_read_frame_chunk() {
   if (!impl_ || !impl_->model) {
      FCL_THROW_EXCEPTION(exceptions::closed, "invalid transport stream");
   }
   while (true) {
      const auto decoded = decode_frame_view(available_bytes(impl_->buffer, impl_->consumed));
      if (decoded.status == frame_decode_status::complete) {
         const auto payload_size = decoded.payload.size();
         const auto payload_offset = static_cast<std::size_t>(decoded.payload.data() - impl_->buffer.data());
         if (impl_->consumed == 0 && decoded.consumed == impl_->buffer.size()) {
            auto storage = std::move(impl_->buffer);
            impl_->buffer.clear();
            impl_->consumed = 0;
            co_return chunk{std::move(storage), payload_offset, payload_size};
         }
         auto payload = chunk{decoded.payload};
         impl_->consumed += decoded.consumed;
         if (impl_->consumed >= impl_->buffer.size() || impl_->consumed > compact_threshold) {
            compact_buffer(impl_->buffer, impl_->consumed);
         }
         co_return payload;
      }
      compact_buffer(impl_->buffer, impl_->consumed);
      auto next = co_await impl_->model->async_read_chunk();
      auto view = next.bytes();
      if (view.empty()) {
         continue;
      }
      impl_->buffer.insert(impl_->buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_ || !impl_->model) {
      co_return;
   }
   co_await impl_->model->async_close();
}

void stream::cancel() {
   if (impl_ && impl_->model) {
      impl_->model->cancel();
   }
}

stream detail::stream_access::make(std::shared_ptr<stream_concept> model) {
   return stream{std::move(model)};
}

stream detail::stream_access::with_buffer(stream value, std::vector<std::uint8_t> buffered) {
   if (!value.impl_ || buffered.empty()) {
      return value;
   }
   compact_buffer(value.impl_->buffer, value.impl_->consumed);
   if (!value.impl_->buffer.empty()) {
      value.impl_->buffer.insert(value.impl_->buffer.end(), buffered.begin(), buffered.end());
      return value;
   }
   value.impl_->buffer = std::move(buffered);
   return value;
}

boost::asio::awaitable<void> detail::stream_concept::async_write_chunk(chunk bytes) {
   co_await async_write(bytes.bytes());
}

boost::asio::awaitable<chunk> detail::stream_concept::async_read_chunk() {
   co_return chunk{co_await async_read()};
}

} // namespace fcl::transport
