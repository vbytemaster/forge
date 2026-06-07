module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

module fcl.transport.buffer;

namespace fcl::transport {
namespace {

[[noreturn]] void throw_invalid_buffer(std::string message) {
   FCL_THROW_EXCEPTION(exceptions::invalid_buffer, std::move(message));
}

[[nodiscard]] std::size_t effective_capacity(const buffer_pool_options& options, std::size_t requested) noexcept {
   return requested == 0 ? options.default_capacity : requested;
}

} // namespace

namespace detail {

struct buffer_pool_state {
   explicit buffer_pool_state(buffer_pool_options value) : options(value) {}

   [[nodiscard]] std::vector<std::uint8_t> acquire(std::size_t requested) {
      const auto capacity = effective_capacity(options, requested);
      auto lock = std::scoped_lock{mutex};
      for (auto it = cached.begin(); it != cached.end(); ++it) {
         if (it->capacity() >= capacity) {
            auto out = std::move(*it);
            cached_bytes -= out.capacity();
            cached.erase(it);
            out.resize(capacity);
            return out;
         }
      }
      return std::vector<std::uint8_t>(capacity);
   }

   void release(std::vector<std::uint8_t> value) {
      const auto capacity = value.capacity();
      if (capacity == 0 || options.max_cached_buffers == 0 || options.max_cached_bytes == 0) {
         return;
      }
      auto lock = std::scoped_lock{mutex};
      if (capacity > options.max_cached_buffer_capacity || cached.size() >= options.max_cached_buffers ||
          cached_bytes >= options.max_cached_bytes || capacity > options.max_cached_bytes - cached_bytes) {
         return;
      }
      value.clear();
      cached_bytes += capacity;
      cached.push_back(std::move(value));
   }

   [[nodiscard]] buffer_pool_stats stats() const {
      auto lock = std::scoped_lock{mutex};
      return {.buffers = cached.size(), .bytes = cached_bytes};
   }

   buffer_pool_options options;
   mutable std::mutex mutex;
   std::deque<std::vector<std::uint8_t>> cached;
   std::size_t cached_bytes = 0;
};

} // namespace detail

struct chunk::storage {
   explicit storage(std::vector<std::uint8_t> value) : data(std::move(value)) {}
   storage(std::vector<std::uint8_t> value, std::weak_ptr<detail::buffer_pool_state> owner)
       : data(std::move(value)), pool(std::move(owner)) {}

   ~storage() {
      if (auto owner = pool.lock()) {
         owner->release(std::move(data));
      }
   }

   std::vector<std::uint8_t> data;
   std::weak_ptr<detail::buffer_pool_state> pool;
};

chunk::chunk() = default;

chunk::chunk(std::span<const std::uint8_t> bytes)
    : chunk(std::vector<std::uint8_t>{bytes.begin(), bytes.end()}) {}

chunk::chunk(std::vector<std::uint8_t> bytes)
    : storage_(std::make_shared<storage>(std::move(bytes))), offset_(0), size_(storage_->data.size()) {}

chunk::chunk(std::vector<std::uint8_t> bytes, std::size_t offset, std::size_t size)
    : storage_(std::make_shared<storage>(std::move(bytes))), offset_(offset), size_(size) {
   if (offset > storage_->data.size() || size > storage_->data.size() - offset) {
      throw_invalid_buffer("chunk range exceeds backing storage");
   }
}

chunk::chunk(std::shared_ptr<storage> value, std::size_t offset, std::size_t size)
    : storage_(std::move(value)), offset_(offset), size_(size) {
   if (!storage_) {
      if (offset != 0 || size != 0) {
         throw_invalid_buffer("chunk range requires backing storage");
      }
      return;
   }
   if (offset > storage_->data.size() || size > storage_->data.size() - offset) {
      throw_invalid_buffer("chunk range exceeds backing storage");
   }
}

chunk::~chunk() = default;
chunk::chunk(const chunk&) noexcept = default;
chunk& chunk::operator=(const chunk&) noexcept = default;
chunk::chunk(chunk&&) noexcept = default;
chunk& chunk::operator=(chunk&&) noexcept = default;

bool chunk::empty() const noexcept {
   return size_ == 0;
}

std::size_t chunk::size() const noexcept {
   return size_;
}

std::span<const std::uint8_t> chunk::bytes() const noexcept {
   if (!storage_ || size_ == 0) {
      return {};
   }
   return {storage_->data.data() + offset_, size_};
}

std::vector<std::uint8_t> chunk::to_vector() const {
   const auto view = bytes();
   return {view.begin(), view.end()};
}

std::vector<std::uint8_t> chunk::into_vector() && {
   if (!storage_) {
      return {};
   }
   if (storage_.use_count() == 1 && offset_ == 0 && size_ == storage_->data.size()) {
      auto out = std::move(storage_->data);
      storage_.reset();
      offset_ = 0;
      size_ = 0;
      return out;
   }
   return to_vector();
}

chunk_builder::chunk_builder() = default;

chunk_builder::chunk_builder(std::shared_ptr<detail::buffer_pool_state> pool, std::vector<std::uint8_t> buffer)
    : pool_(std::move(pool)), buffer_(std::move(buffer)), active_(true) {}

chunk_builder::~chunk_builder() {
   if (active_ && pool_) {
      pool_->release(std::move(buffer_));
   }
}

chunk_builder::chunk_builder(chunk_builder&&) noexcept = default;

chunk_builder& chunk_builder::operator=(chunk_builder&& other) noexcept {
   if (this != &other) {
      if (active_ && pool_) {
         pool_->release(std::move(buffer_));
      }
      pool_ = std::move(other.pool_);
      buffer_ = std::move(other.buffer_);
      active_ = other.active_;
      other.active_ = false;
   }
   return *this;
}

bool chunk_builder::valid() const noexcept {
   return active_;
}

std::span<std::uint8_t> chunk_builder::writable() {
   if (!active_) {
      throw_invalid_buffer("chunk builder is no longer active");
   }
   return buffer_;
}

chunk chunk_builder::commit(std::size_t size) {
   if (!active_) {
      throw_invalid_buffer("chunk builder is no longer active");
   }
   if (size > buffer_.size()) {
      throw_invalid_buffer("chunk commit size exceeds writable buffer");
   }
   buffer_.resize(size);
   auto value = chunk{std::make_shared<chunk::storage>(std::move(buffer_), pool_), 0, size};
   active_ = false;
   pool_.reset();
   return value;
}

buffer_pool::buffer_pool() : buffer_pool(buffer_pool_options{}) {}

buffer_pool::buffer_pool(buffer_pool_options options) : state_(std::make_shared<detail::buffer_pool_state>(options)) {}

buffer_pool::~buffer_pool() = default;
buffer_pool::buffer_pool(const buffer_pool&) noexcept = default;
buffer_pool& buffer_pool::operator=(const buffer_pool&) noexcept = default;
buffer_pool::buffer_pool(buffer_pool&&) noexcept = default;
buffer_pool& buffer_pool::operator=(buffer_pool&&) noexcept = default;

chunk_builder buffer_pool::acquire(std::size_t capacity) {
   if (!state_) {
      state_ = std::make_shared<detail::buffer_pool_state>(buffer_pool_options{});
   }
   return chunk_builder{state_, state_->acquire(capacity)};
}

chunk buffer_pool::copy(std::span<const std::uint8_t> bytes) {
   auto builder = acquire(bytes.size());
   auto writable = builder.writable();
   std::copy(bytes.begin(), bytes.end(), writable.begin());
   return builder.commit(bytes.size());
}

buffer_pool_stats buffer_pool::cached() const {
   if (!state_) {
      return {};
   }
   return state_->stats();
}

} // namespace fcl::transport
