module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

module fcl.multiformats.varint;

namespace fcl::multiformats {

bytes varint_encode(std::uint64_t value) {
   auto out = bytes{};
   do {
      auto byte = static_cast<std::uint8_t>(value & 0x7fU);
      value >>= 7U;
      if (value != 0) {
         byte |= 0x80U;
      }
      out.push_back(byte);
   } while (value != 0);
   return out;
}

decoded_varint varint_decode(std::span<const std::uint8_t> data) {
   std::uint64_t value = 0;
   std::uint32_t shift = 0;
   for (std::size_t index = 0; index < data.size(); ++index) {
      const auto byte = data[index];
      if (shift >= 64 && (byte & 0x7fU) != 0) {
         throw format_error{"multiformats varint overflows uint64"};
      }

      value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
      if ((byte & 0x80U) == 0) {
         const auto encoded = varint_encode(value);
         if (encoded.size() != index + 1) {
            throw format_error{"multiformats varint is not minimally encoded"};
         }
         return {.value = value, .size = index + 1};
      }
      shift += 7;
   }

   throw format_error{"unterminated multiformats varint"};
}

} // namespace fcl::multiformats
