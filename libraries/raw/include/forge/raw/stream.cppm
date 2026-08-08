module;

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.raw.stream;

export namespace forge {

static_assert(CHAR_BIT == 8, "Forge datastream requires 8-bit bytes");

namespace raw::detail {
[[noreturn]] void raise_stream_range(const char* operation, std::size_t length, std::int64_t overrun);

struct allocation_limits {
   std::size_t elements = std::numeric_limits<std::size_t>::max();
   std::size_t total_elements = std::numeric_limits<std::size_t>::max();
   std::size_t bytes = std::numeric_limits<std::size_t>::max();
   std::size_t first_container_elements = std::numeric_limits<std::size_t>::max();
};
} // namespace raw::detail

template <typename Storage, typename Enable = void> class datastream;

template <typename Storage>
class datastream<Storage, std::enable_if_t<std::is_same_v<Storage, char*> || std::is_same_v<Storage, const char*> ||
                                           std::is_same_v<Storage, unsigned char*> ||
                                           std::is_same_v<Storage, const unsigned char*>>> {
 public:
   datastream(Storage start, std::size_t size) : start_(start), position_(start), end_(start + size) {}

   void skip(std::size_t size) {
      if (size > remaining()) {
         raw::detail::raise_stream_range("skip", length(), static_cast<std::int64_t>(size - remaining()));
      }
      position_ += size;
   }

   bool read(char* destination, std::size_t size) {
      if (size > remaining()) {
         raw::detail::raise_stream_range("read", length(), static_cast<std::int64_t>(size - remaining()));
      }
      std::memcpy(destination, position_, size);
      position_ += size;
      return true;
   }

   bool write(const char* source, std::size_t size) {
      if (size > remaining()) {
         raw::detail::raise_stream_range("write", length(), static_cast<std::int64_t>(size - remaining()));
      }
      std::memcpy(position_, source, size);
      position_ += size;
      return true;
   }

   bool put(char value) {
      return write(&value, 1);
   }

   bool get(unsigned char& value) {
      return read(reinterpret_cast<char*>(&value), 1);
   }

   bool get(char& value) {
      return read(&value, 1);
   }

   Storage pos() const {
      return position_;
   }

   bool valid() const {
      return position_ >= start_ && position_ <= end_;
   }

   bool seekp(std::size_t position) {
      if (position > length()) {
         return false;
      }
      position_ = start_ + position;
      return true;
   }

   std::size_t tellp() const {
      return static_cast<std::size_t>(position_ - start_);
   }

   std::size_t remaining() const {
      return static_cast<std::size_t>(end_ - position_);
   }

 private:
   std::size_t length() const {
      return static_cast<std::size_t>(end_ - start_);
   }

   Storage start_;
   Storage position_;
   Storage end_;
};

template <> class datastream<std::size_t, void> {
 public:
   explicit datastream(std::size_t initial_size = 0) : size_(initial_size) {}

   bool skip(std::size_t size) {
      size_ += size;
      return true;
   }

   bool write(const char*, std::size_t size) {
      size_ += size;
      return true;
   }

   bool put(char) {
      ++size_;
      return true;
   }

   bool valid() const {
      return true;
   }

   bool seekp(std::size_t position) {
      size_ = position;
      return true;
   }

   std::size_t tellp() const {
      return size_;
   }

   std::size_t remaining() const {
      return 0;
   }

 private:
   std::size_t size_;
};

template <typename Byte, typename Allocator>
class datastream<std::vector<Byte, Allocator>,
                 std::enable_if_t<std::is_same_v<Byte, char> || std::is_same_v<Byte, std::uint8_t>>> {
 public:
   using storage_type = std::vector<Byte, Allocator>;

   explicit datastream(storage_type storage = {}, raw::detail::allocation_limits allocation_limits = {})
       : storage_(std::move(storage)), allocation_limits_(allocation_limits),
         remaining_elements_(allocation_limits.total_elements) {}

   std::size_t read(char* destination, std::size_t size) {
      if (size > remaining()) {
         raw::detail::raise_stream_range("read", storage_.size(), static_cast<std::int64_t>(size - remaining()));
      }
      std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(position_), size, destination);
      position_ += size;
      return size;
   }

   std::size_t write(const char* source, std::size_t size) {
      if (position_ > storage_.size() || size > storage_.max_size() - position_) {
         raw::detail::raise_stream_range("write", storage_.size(), static_cast<std::int64_t>(size));
      }
      storage_.resize(std::max(position_ + size, storage_.size()));
      std::copy_n(source, size, storage_.begin() + static_cast<std::ptrdiff_t>(position_));
      position_ += size;
      return size;
   }

   bool seekp(std::size_t position) {
      if (position > storage_.size()) {
         return false;
      }
      position_ = position;
      return true;
   }

   std::size_t tellp() const {
      return position_;
   }

   bool skip(std::size_t size) {
      return seekp(position_ + size);
   }

   bool get(char& value) {
      read(&value, 1);
      return true;
   }

   bool get(std::uint8_t& value) {
      read(reinterpret_cast<char*>(&value), 1);
      return true;
   }

   std::size_t remaining() const {
      return storage_.size() - position_;
   }

   [[nodiscard]] const raw::detail::allocation_limits& allocation_limits() const noexcept {
      return allocation_limits_;
   }

   bool consume_container_elements(std::size_t count) noexcept {
      const auto limit =
          std::min({allocation_limits_.elements, allocation_limits_.first_container_elements, remaining_elements_});
      allocation_limits_.first_container_elements = std::numeric_limits<std::size_t>::max();
      if (count > limit) {
         return false;
      }
      remaining_elements_ -= count;
      return true;
   }

   storage_type& storage() {
      return storage_;
   }

   const storage_type& storage() const {
      return storage_;
   }

 private:
   storage_type storage_;
   std::size_t position_ = 0;
   raw::detail::allocation_limits allocation_limits_;
   std::size_t remaining_elements_;
};

template <typename Storage> datastream<Storage>& operator<<(datastream<Storage>& stream, const __int128& value) {
   stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
   return stream;
}

template <typename Storage> datastream<Storage>& operator>>(datastream<Storage>& stream, __int128& value) {
   stream.read(reinterpret_cast<char*>(&value), sizeof(value));
   return stream;
}

template <typename Storage>
datastream<Storage>& operator<<(datastream<Storage>& stream, const unsigned __int128& value) {
   stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
   return stream;
}

template <typename Storage> datastream<Storage>& operator>>(datastream<Storage>& stream, unsigned __int128& value) {
   stream.read(reinterpret_cast<char*>(&value), sizeof(value));
   return stream;
}

#define FORGE_RAW_STREAM_SCALAR(Type)                                                                                  \
   template <typename Storage> datastream<Storage>& operator<<(datastream<Storage>& stream, const Type& value) {       \
      stream.write(reinterpret_cast<const char*>(&value), sizeof(value));                                              \
      return stream;                                                                                                   \
   }                                                                                                                   \
   template <typename Storage> datastream<Storage>& operator>>(datastream<Storage>& stream, Type& value) {             \
      stream.read(reinterpret_cast<char*>(&value), sizeof(value));                                                     \
      return stream;                                                                                                   \
   }

FORGE_RAW_STREAM_SCALAR(std::int64_t)
FORGE_RAW_STREAM_SCALAR(std::uint64_t)
FORGE_RAW_STREAM_SCALAR(std::int32_t)
FORGE_RAW_STREAM_SCALAR(std::uint32_t)
FORGE_RAW_STREAM_SCALAR(std::int16_t)
FORGE_RAW_STREAM_SCALAR(std::uint16_t)
FORGE_RAW_STREAM_SCALAR(std::int8_t)
FORGE_RAW_STREAM_SCALAR(std::uint8_t)

#undef FORGE_RAW_STREAM_SCALAR

} // namespace forge
