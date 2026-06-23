module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module forge.crypto.secret_bytes;

import forge.crypto.types;

export namespace forge::crypto {

class secret_bytes {
 public:
   secret_bytes() = default;
   explicit secret_bytes(bytes value);
   explicit secret_bytes(std::span<const std::uint8_t> value);
   ~secret_bytes();

   secret_bytes(secret_bytes&& other) noexcept;
   secret_bytes& operator=(secret_bytes&& other) noexcept;

   secret_bytes(const secret_bytes&) = delete;
   secret_bytes& operator=(const secret_bytes&) = delete;

   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] std::size_t size() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> span() const noexcept;
   [[nodiscard]] bytes copy() const;

   void assign(bytes value);
   void clear() noexcept;

 private:
   bytes value_;
};

} // namespace forge::crypto
