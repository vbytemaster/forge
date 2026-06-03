#pragma once

#include <optional>
#include <string>

namespace fcl::p2p {

struct libp2p_tls_material {
   std::string certificate_pem;
   std::string private_key_pem;
};

[[nodiscard]] libp2p_tls_material make_libp2p_tls_material(const node::options& options);
[[nodiscard]] peer_id verify_libp2p_tls_chain(const fcl::stcp::certificate_chain& chain,
                                              const std::optional<peer_id>& expected_peer);
[[nodiscard]] fcl::stcp::client_options make_libp2p_tls_client_options(const node::options& options);
[[nodiscard]] fcl::stcp::server_options make_libp2p_tls_server_options(const node::options& options);

} // namespace fcl::p2p
