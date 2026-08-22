#pragma once

namespace forge::net::transport::detail {

class bounded_frame_buffer {
 public:
   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] std::size_t size() const noexcept;
   [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

   void append(std::span<const std::uint8_t> bytes, frame_options options);
   void append_prefetched(std::vector<std::uint8_t> bytes);
   void enforce_limit(frame_options options) const;

   [[nodiscard]] chunk take_all();
   [[nodiscard]] chunk take_frame_payload(std::size_t frame_size, std::size_t payload_size);

 private:
   std::vector<std::uint8_t> bytes_;
};

} // namespace forge::net::transport::detail
