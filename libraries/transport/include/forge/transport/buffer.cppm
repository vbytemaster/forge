module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module forge.transport.buffer;

import forge.transport.exceptions;

export namespace forge::transport {

struct buffer_pool_options {
   std::size_t default_capacity = 64 * 1024;
   std::size_t max_cached_buffers = 64;
   std::size_t max_cached_bytes = 16 * 1024 * 1024;
   std::size_t max_cached_buffer_capacity = 1 * 1024 * 1024;
};

struct buffer_pool_stats {
   std::size_t buffers = 0;
   std::size_t bytes = 0;
};

namespace detail {
struct buffer_pool_state;
} // namespace detail

class chunk {
 public:
   chunk();
   explicit chunk(std::span<const std::uint8_t> bytes);
   explicit chunk(std::vector<std::uint8_t> bytes);
   chunk(std::vector<std::uint8_t> bytes, std::size_t offset, std::size_t size);
   ~chunk();

   chunk(const chunk&) noexcept;
   chunk& operator=(const chunk&) noexcept;
   chunk(chunk&&) noexcept;
   chunk& operator=(chunk&&) noexcept;

   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] std::size_t size() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
   [[nodiscard]] std::vector<std::uint8_t> to_vector() const;
   [[nodiscard]] std::vector<std::uint8_t> into_vector() &&;

 private:
   friend class chunk_builder;

   struct storage;
   explicit chunk(std::shared_ptr<storage> value, std::size_t offset, std::size_t size);

   std::shared_ptr<storage> storage_;
   std::size_t offset_ = 0;
   std::size_t size_ = 0;
};

class chunk_builder {
 public:
   chunk_builder();
   ~chunk_builder();

   chunk_builder(const chunk_builder&) = delete;
   chunk_builder& operator=(const chunk_builder&) = delete;
   chunk_builder(chunk_builder&&) noexcept;
   chunk_builder& operator=(chunk_builder&&) noexcept;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] std::span<std::uint8_t> writable();
   [[nodiscard]] chunk commit(std::size_t size);

 private:
   friend class buffer_pool;

   chunk_builder(std::shared_ptr<detail::buffer_pool_state> pool, std::vector<std::uint8_t> buffer);

   std::shared_ptr<detail::buffer_pool_state> pool_;
   std::vector<std::uint8_t> buffer_;
   bool active_ = false;
};

class buffer_pool {
 public:
   buffer_pool();
   explicit buffer_pool(buffer_pool_options options);
   ~buffer_pool();

   buffer_pool(const buffer_pool&) noexcept;
   buffer_pool& operator=(const buffer_pool&) noexcept;
   buffer_pool(buffer_pool&&) noexcept;
   buffer_pool& operator=(buffer_pool&&) noexcept;

   [[nodiscard]] chunk_builder acquire(std::size_t capacity = 0);
   [[nodiscard]] chunk copy(std::span<const std::uint8_t> bytes);
   [[nodiscard]] buffer_pool_stats cached() const;

 private:
   std::shared_ptr<detail::buffer_pool_state> state_;
};

} // namespace forge::transport
