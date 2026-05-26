module;

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

module fcl.multiformats.address;

import fcl.multiformats.exceptions;

import fcl.crypto.base58;
import fcl.multiformats.varint;

namespace fcl::multiformats {
namespace {

[[nodiscard]] std::vector<std::string_view> split_path(std::string_view value) {
   if (value.empty() || value.front() != '/') {
      throw exceptions::invalid_format{"address must start with '/'"};
   }

   auto parts = std::vector<std::string_view>{};
   std::size_t begin = 1;
   while (begin <= value.size()) {
      const auto end = value.find('/', begin);
      auto part = value.substr(begin, end == std::string_view::npos ? end : end - begin);
      if (part.empty()) {
         throw exceptions::invalid_format{"address contains an empty component"};
      }
      parts.push_back(part);
      if (end == std::string_view::npos) {
         break;
      }
      begin = end + 1;
   }
   return parts;
}

[[nodiscard]] std::uint16_t parse_port(std::string_view value) {
   std::uint32_t port = 0;
   auto result = std::from_chars(value.data(), value.data() + value.size(), port);
   if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
       port > std::numeric_limits<std::uint16_t>::max()) {
      throw exceptions::invalid_format{"address port is invalid"};
   }
   return static_cast<std::uint16_t>(port);
}

[[nodiscard]] bytes parse_ip(std::string_view value, int family, std::size_t size) {
   auto out = bytes(size);
   const auto text = std::string{value};
   if (inet_pton(family, text.c_str(), out.data()) != 1) {
      throw exceptions::invalid_format{"address IP literal is invalid"};
   }
   return out;
}

[[nodiscard]] std::string format_ip(std::span<const std::uint8_t> data, int family) {
   auto buffer = std::array<char, INET6_ADDRSTRLEN>{};
   if (inet_ntop(family, data.data(), buffer.data(), static_cast<socklen_t>(buffer.size())) == nullptr) {
      throw exceptions::invalid_format{"address IP bytes are invalid"};
   }
   return buffer.data();
}

void append_var(bytes& out, std::uint64_t value) {
   auto encoded = varint_encode(value);
   out.insert(out.end(), encoded.begin(), encoded.end());
}

void append_prefixed(bytes& out, std::span<const std::uint8_t> payload) {
   append_var(out, payload.size());
   out.insert(out.end(), payload.begin(), payload.end());
}

[[nodiscard]] std::string read_prefixed_string(std::span<const std::uint8_t> data, std::size_t& offset) {
   const auto length = varint_decode(data.subspan(offset));
   offset += length.size;
   if (length.value > data.size() - offset) {
      throw exceptions::invalid_format{"address variable component length exceeds available bytes"};
   }
   auto out = std::string{};
   out.reserve(static_cast<std::size_t>(length.value));
   for (std::size_t i = 0; i < length.value; ++i) {
      out.push_back(static_cast<char>(data[offset + i]));
   }
   offset += static_cast<std::size_t>(length.value);
   return out;
}

[[nodiscard]] std::uint16_t read_big_endian_port(std::span<const std::uint8_t> data, std::size_t& offset) {
   if (data.size() - offset < 2) {
      throw exceptions::invalid_format{"address port is truncated"};
   }
   const auto port = static_cast<std::uint16_t>((data[offset] << 8U) | data[offset + 1]);
   offset += 2;
   return port;
}

void append_big_endian_port(bytes& out, std::uint16_t port) {
   out.push_back(static_cast<std::uint8_t>((port >> 8U) & 0xffU));
   out.push_back(static_cast<std::uint8_t>(port & 0xffU));
}

} // namespace

address address::parse(std::string_view value) {
   auto parts = split_path(value);
   auto result = address{};

   for (std::size_t i = 0; i < parts.size(); ++i) {
      auto code = protocol_code(parts[i]);
      switch (code) {
         case multicodec_code::ip4:
         case multicodec_code::ip6:
         case multicodec_code::dns:
         case multicodec_code::dns4:
         case multicodec_code::dns6:
         case multicodec_code::tcp:
         case multicodec_code::udp:
         case multicodec_code::p2p:
            if (++i >= parts.size()) {
               throw exceptions::invalid_format{"address protocol is missing a value"};
            }
            result.push({.code = code, .value = std::string{parts[i]}});
            break;
         case multicodec_code::p2p_circuit:
         case multicodec_code::quic:
         case multicodec_code::quic_v1:
            result.push({.code = code, .value = {}});
            break;
         default:
            throw exceptions::invalid_format{"unsupported address protocol"};
      }
   }

   return result;
}

address address::from_bytes(std::span<const std::uint8_t> data) {
   auto result = address{};
   std::size_t offset = 0;
   while (offset < data.size()) {
      std::size_t consumed = 0;
      auto code = multicodec_decode(data.subspan(offset), consumed);
      offset += consumed;

      switch (code) {
         case multicodec_code::ip4: {
            if (data.size() - offset < 4) {
               throw exceptions::invalid_format{"address ip4 component is truncated"};
            }
            result.push({.code = code, .value = format_ip(data.subspan(offset, 4), AF_INET)});
            offset += 4;
            break;
         }
         case multicodec_code::ip6: {
            if (data.size() - offset < 16) {
               throw exceptions::invalid_format{"address ip6 component is truncated"};
            }
            result.push({.code = code, .value = format_ip(data.subspan(offset, 16), AF_INET6)});
            offset += 16;
            break;
         }
         case multicodec_code::tcp:
         case multicodec_code::udp:
            result.push({.code = code, .value = std::to_string(read_big_endian_port(data, offset))});
            break;
         case multicodec_code::dns:
         case multicodec_code::dns4:
         case multicodec_code::dns6:
            result.push({.code = code, .value = read_prefixed_string(data, offset)});
            break;
         case multicodec_code::p2p: {
            const auto length = varint_decode(data.subspan(offset));
            offset += length.size;
            if (length.value > data.size() - offset) {
               throw exceptions::invalid_format{"address p2p component length exceeds available bytes"};
            }
            auto peer_bytes = bytes{data.begin() + static_cast<std::ptrdiff_t>(offset),
                                    data.begin() + static_cast<std::ptrdiff_t>(offset + length.value)};
            result.push({.code = code, .value = fcl::crypto::base58_encode(peer_bytes)});
            offset += static_cast<std::size_t>(length.value);
            break;
         }
         case multicodec_code::p2p_circuit:
         case multicodec_code::quic:
         case multicodec_code::quic_v1:
            result.push({.code = code, .value = {}});
            break;
         default:
            throw exceptions::invalid_format{"unsupported address protocol"};
      }
   }

   return result;
}

std::string address::to_string() const {
   auto out = std::string{};
   for (const auto& component : components_) {
      out += "/";
      out += protocol_name(component.code);
      if (!component.value.empty()) {
         out += "/";
         out += component.value;
      }
   }
   return out;
}

bytes address::to_bytes() const {
   auto out = bytes{};
   for (const auto& component : components_) {
      append_var(out, code_value(component.code));
      switch (component.code) {
         case multicodec_code::ip4: {
            auto parsed = parse_ip(component.value, AF_INET, 4);
            out.insert(out.end(), parsed.begin(), parsed.end());
            break;
         }
         case multicodec_code::ip6: {
            auto parsed = parse_ip(component.value, AF_INET6, 16);
            out.insert(out.end(), parsed.begin(), parsed.end());
            break;
         }
         case multicodec_code::tcp:
         case multicodec_code::udp:
            append_big_endian_port(out, parse_port(component.value));
            break;
         case multicodec_code::dns:
         case multicodec_code::dns4:
         case multicodec_code::dns6: {
            auto payload = bytes{component.value.begin(), component.value.end()};
            append_prefixed(out, payload);
            break;
         }
         case multicodec_code::p2p: {
            auto payload = fcl::crypto::base58_decode(component.value);
            append_prefixed(out, payload);
            break;
         }
         case multicodec_code::p2p_circuit:
         case multicodec_code::quic:
         case multicodec_code::quic_v1:
            break;
         default:
            throw exceptions::invalid_format{"unsupported address protocol"};
      }
   }
   return out;
}

const std::vector<address::component>& address::components() const noexcept {
   return components_;
}

void address::push(component value) {
   components_.push_back(std::move(value));
}

} // namespace fcl::multiformats
