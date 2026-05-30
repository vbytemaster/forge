module;

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fcl.p2p.identity;

import fcl.p2p.exceptions;
import fcl.multiformats;

export namespace fcl::p2p {

struct public_key {
   enum class type : std::uint8_t {
      rsa = 0,
      ed25519 = 1,
      secp256k1 = 2,
      ecdsa = 3,
   };

   type type;
   std::vector<std::uint8_t> data;
};

struct peer_id {
   std::string value;

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] std::string to_cid_string() const;
   [[nodiscard]] fcl::multiformats::bytes to_bytes() const;

   [[nodiscard]] static peer_id from_string(std::string_view value);
   [[nodiscard]] static peer_id from_bytes(std::span<const std::uint8_t> value);

   [[nodiscard]] friend bool operator==(const peer_id&, const peer_id&) noexcept = default;
   [[nodiscard]] friend auto operator<=>(const peer_id&, const peer_id&) noexcept = default;
};

[[nodiscard]] fcl::multiformats::bytes encode_public_key(const public_key& key);
[[nodiscard]] public_key decode_public_key(std::span<const std::uint8_t> bytes);
[[nodiscard]] peer_id make_peer_id(const public_key& key);
[[nodiscard]] peer_id make_peer_id_from_certificate_pem(std::string_view certificate_pem);
[[nodiscard]] peer_id make_peer_id_from_certificate_der(std::span<const std::uint8_t> certificate_der);
[[nodiscard]] bool valid_peer_id(const peer_id& id) noexcept;

} // namespace fcl::p2p
