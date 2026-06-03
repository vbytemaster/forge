module;

#include <string>
#include <string_view>

module fcl.quic.libp2p;

namespace fcl::quic::libp2p {

client_options client_profile(client_options options) {
   options.alpn = std::string{alpn};
   return options;
}

server_options server_profile(server_options options) {
   options.alpn = std::string{alpn};
   return options;
}

bool is_profile_alpn(std::string_view value) noexcept {
   return value == alpn;
}

} // namespace fcl::quic::libp2p
