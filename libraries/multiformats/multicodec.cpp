module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module fcl.multiformats.multicodec;

import fcl.multiformats.varint;

namespace fcl::multiformats {
namespace {

struct protocol_entry {
   std::string_view name;
   multicodec_code code;
};

constexpr auto protocols = std::array{
    protocol_entry{.name = "ip4", .code = multicodec_code::ip4},
    protocol_entry{.name = "ip6", .code = multicodec_code::ip6},
    protocol_entry{.name = "dns", .code = multicodec_code::dns},
    protocol_entry{.name = "dns4", .code = multicodec_code::dns4},
    protocol_entry{.name = "dns6", .code = multicodec_code::dns6},
    protocol_entry{.name = "tcp", .code = multicodec_code::tcp},
    protocol_entry{.name = "udp", .code = multicodec_code::udp},
    protocol_entry{.name = "quic-v1", .code = multicodec_code::quic_v1},
    protocol_entry{.name = "p2p", .code = multicodec_code::p2p},
};

} // namespace

bytes multicodec_encode(multicodec_code code) {
   return varint_encode(code_value(code));
}

multicodec_code multicodec_decode(std::span<const std::uint8_t> data, std::size_t& consumed) {
   const auto decoded = varint_decode(data);
   consumed = decoded.size;
   switch (decoded.value) {
      case code_value(multicodec_code::identity):
         return multicodec_code::identity;
      case code_value(multicodec_code::sha2_256):
         return multicodec_code::sha2_256;
      case code_value(multicodec_code::sha2_512):
         return multicodec_code::sha2_512;
      case code_value(multicodec_code::libp2p_key):
         return multicodec_code::libp2p_key;
      case code_value(multicodec_code::ip4):
         return multicodec_code::ip4;
      case code_value(multicodec_code::tcp):
         return multicodec_code::tcp;
      case code_value(multicodec_code::ip6):
         return multicodec_code::ip6;
      case code_value(multicodec_code::dns):
         return multicodec_code::dns;
      case code_value(multicodec_code::dns4):
         return multicodec_code::dns4;
      case code_value(multicodec_code::dns6):
         return multicodec_code::dns6;
      case code_value(multicodec_code::udp):
         return multicodec_code::udp;
      case code_value(multicodec_code::p2p):
         return multicodec_code::p2p;
      case code_value(multicodec_code::quic_v1):
         return multicodec_code::quic_v1;
      default:
         throw format_error{"unsupported multicodec code"};
   }
}

std::string_view protocol_name(multicodec_code code) {
   for (const auto& entry : protocols) {
      if (entry.code == code) {
         return entry.name;
      }
   }
   throw format_error{"multicodec code is not an address protocol"};
}

multicodec_code protocol_code(std::string_view name) {
   auto canonical = name == "ipfs" ? std::string_view{"p2p"} : name;
   for (const auto& entry : protocols) {
      if (entry.name == canonical) {
         return entry.code;
      }
   }
   throw format_error{"unsupported address protocol"};
}

} // namespace fcl::multiformats
