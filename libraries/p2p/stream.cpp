module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

module forge.p2p.stream;

import forge.p2p.exceptions;
import forge.transport.frame;

namespace forge::p2p {
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
   forge::transport::stream transport;
   std::vector<std::uint8_t> buffer;
   std::size_t consumed = 0;
};

stream::stream() = default;

stream::stream(forge::transport::stream value) : impl_(std::make_shared<impl>()) {
   impl_->transport = std::move(value);
}

stream::stream(forge::transport::stream value, std::vector<std::uint8_t> buffered) : stream{std::move(value)} {
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
   co_await async_write(forge::transport::chunk{bytes});
}

boost::asio::awaitable<void> stream::async_write(forge::transport::chunk bytes) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   co_await impl_->transport.async_write(std::move(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read() {
   auto value = co_await async_read_chunk();
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<forge::transport::chunk> stream::async_read_chunk() {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   if (!available_bytes(impl_->buffer, impl_->consumed).empty()) {
      auto out = forge::transport::chunk{available_bytes(impl_->buffer, impl_->consumed)};
      impl_->buffer.clear();
      impl_->consumed = 0;
      co_return out;
   }
   co_return co_await impl_->transport.async_read_chunk();
}

boost::asio::awaitable<void> stream::async_write_frame(std::span<const std::uint8_t> bytes) {
   co_await async_write_frame(forge::transport::chunk{bytes});
}

boost::asio::awaitable<void> stream::async_write_frame(forge::transport::chunk bytes) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   co_await impl_->transport.async_write_frame(std::move(bytes));
}

boost::asio::awaitable<std::vector<std::uint8_t>> stream::async_read_frame() {
   auto value = co_await async_read_frame_chunk();
   co_return std::move(value).into_vector();
}

boost::asio::awaitable<forge::transport::chunk> stream::async_read_frame_chunk() {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   if (impl_->buffer.empty()) {
      co_return co_await impl_->transport.async_read_frame_chunk();
   }
   while (true) {
      const auto decoded = forge::transport::decode_frame_view(available_bytes(impl_->buffer, impl_->consumed));
      if (decoded.status == forge::transport::frame_decode_status::complete) {
         const auto payload_size = decoded.payload.size();
         const auto payload_offset = static_cast<std::size_t>(decoded.payload.data() - impl_->buffer.data());
         if (impl_->consumed == 0 && decoded.consumed == impl_->buffer.size()) {
            auto storage = std::move(impl_->buffer);
            impl_->buffer.clear();
            impl_->consumed = 0;
            co_return forge::transport::chunk{std::move(storage), payload_offset, payload_size};
         }
         auto payload = forge::transport::chunk{decoded.payload};
         impl_->consumed += decoded.consumed;
         if (impl_->consumed >= impl_->buffer.size() || impl_->consumed > compact_threshold) {
            compact_buffer(impl_->buffer, impl_->consumed);
         }
         co_return payload;
      }
      compact_buffer(impl_->buffer, impl_->consumed);
      auto chunk = co_await impl_->transport.async_read_chunk();
      auto view = chunk.bytes();
      if (view.empty()) {
         continue;
      }
      impl_->buffer.insert(impl_->buffer.end(), view.begin(), view.end());
   }
}

boost::asio::awaitable<void> stream::async_close() {
   if (!impl_) {
      co_return;
   }
   co_await impl_->transport.async_close();
}

void stream::cancel() {
   if (impl_) {
      impl_->transport.cancel();
   }
}

forge::transport::stream stream::into_transport_stream() && {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::closed, "invalid P2P stream");
   }
   compact_buffer(impl_->buffer, impl_->consumed);
   auto transport = std::move(impl_->transport);
   auto buffered = std::move(impl_->buffer);
   impl_.reset();
   return forge::transport::detail::stream_access::with_buffer(std::move(transport), std::move(buffered));
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
   value.impl_->consumed = 0;
   return value;
}

} // namespace forge::p2p
