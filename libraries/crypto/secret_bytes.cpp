module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

module fcl.crypto.secret_bytes;

namespace fcl::crypto {

namespace {

void wipe(bytes& value) noexcept {
   std::fill(value.begin(), value.end(), std::uint8_t{0});
   value.clear();
   value.shrink_to_fit();
}

} // namespace

secret_bytes::secret_bytes(bytes value) : value_{std::move(value)} {}

secret_bytes::secret_bytes(std::span<const std::uint8_t> value) : value_{value.begin(), value.end()} {}

secret_bytes::~secret_bytes() {
   clear();
}

secret_bytes::secret_bytes(secret_bytes&& other) noexcept : value_{std::move(other.value_)} {
   other.value_.clear();
}

secret_bytes& secret_bytes::operator=(secret_bytes&& other) noexcept {
   if (this != &other) {
      clear();
      value_ = std::move(other.value_);
      other.value_.clear();
   }
   return *this;
}

bool secret_bytes::empty() const noexcept {
   return value_.empty();
}

std::size_t secret_bytes::size() const noexcept {
   return value_.size();
}

std::span<const std::uint8_t> secret_bytes::span() const noexcept {
   return std::span<const std::uint8_t>{value_.data(), value_.size()};
}

bytes secret_bytes::copy() const {
   return value_;
}

void secret_bytes::assign(bytes value) {
   clear();
   value_ = std::move(value);
}

void secret_bytes::clear() noexcept {
   wipe(value_);
}

} // namespace fcl::crypto
