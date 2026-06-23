module;

#include <string>
#include <span>

module forge.multiformats.multihash;

import forge.multiformats.exceptions;

import forge.crypto.sha256;
import forge.crypto.sha512;
import forge.multiformats.varint;

namespace forge::multiformats {
namespace {

[[nodiscard]] std::string bytes_to_string(std::span<const std::uint8_t> data) {
   auto out = std::string{};
   out.reserve(data.size());
   for (auto byte : data) {
      out.push_back(static_cast<char>(byte));
   }
   return out;
}

[[nodiscard]] bytes bytes_from_chars(const char* data, std::size_t size) {
   auto out = bytes{};
   out.reserve(size);
   for (std::size_t i = 0; i < size; ++i) {
      out.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(data[i])));
   }
   return out;
}

} // namespace

bytes multihash::encode() const {
   auto out = varint_encode(code);
   auto size = varint_encode(digest.size());
   out.insert(out.end(), size.begin(), size.end());
   out.insert(out.end(), digest.begin(), digest.end());
   return out;
}

std::string multihash::digest_hex() const {
   static constexpr auto digits = "0123456789abcdef";
   auto out = std::string{};
   out.reserve(digest.size() * 2);
   for (auto byte : digest) {
      out.push_back(digits[(byte >> 4U) & 0x0fU]);
      out.push_back(digits[byte & 0x0fU]);
   }
   return out;
}

multihash multihash::decode(std::span<const std::uint8_t> data) {
   auto code = varint_decode(data);
   if (code.size >= data.size()) {
      throw exceptions::invalid_format{"multihash is missing digest length"};
   }

   auto length = varint_decode(data.subspan(code.size));
   const auto offset = code.size + length.size;
   if (length.value > data.size() - offset) {
      throw exceptions::invalid_format{"multihash digest length exceeds available bytes"};
   }
   if (length.value != data.size() - offset) {
      throw exceptions::invalid_format{"multihash has trailing bytes"};
   }

   return {.code = code.value,
           .digest = bytes{data.begin() + static_cast<std::ptrdiff_t>(offset), data.end()}};
}

multihash multihash::identity(std::span<const std::uint8_t> data) {
   return {.code = code_value(multicodec_code::identity), .digest = bytes{data.begin(), data.end()}};
}

multihash multihash::sha2_256(std::span<const std::uint8_t> data) {
   const auto input = bytes_to_string(data);
   const auto digest = forge::crypto::sha256::hash(input.data(), static_cast<std::uint32_t>(input.size()));
   return {.code = code_value(multicodec_code::sha2_256), .digest = bytes_from_chars(digest.data(), digest.data_size())};
}

multihash multihash::sha2_512(std::span<const std::uint8_t> data) {
   const auto input = bytes_to_string(data);
   const auto digest = forge::crypto::sha512::hash(input.data(), static_cast<std::uint32_t>(input.size()));
   return {.code = code_value(multicodec_code::sha2_512), .digest = bytes_from_chars(digest.data(), digest.data_size())};
}

} // namespace forge::multiformats
