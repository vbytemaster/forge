module;

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module forge.p2p.envelope;

import forge.crypto.asymmetric;
import forge.p2p.identity;

export namespace forge::p2p {

struct signed_envelope {
   public_key key;
   std::vector<std::uint8_t> payload_type;
   std::vector<std::uint8_t> payload;
   std::vector<std::uint8_t> signature;

   [[nodiscard]] peer_id signer() const;
   [[nodiscard]] std::vector<std::uint8_t> signing_payload(std::string_view domain) const;
   [[nodiscard]] std::vector<std::uint8_t> encode() const;
   void verify(std::string_view domain, std::optional<peer_id> expected_signer = std::nullopt) const;

   [[nodiscard]] static signed_envelope decode(std::span<const std::uint8_t> bytes);
   [[nodiscard]] static signed_envelope seal(const public_key& key, const forge::crypto::asymmetric::private_key& private_key,
                                             std::string_view domain,
                                             std::span<const std::uint8_t> payload_type,
                                             std::span<const std::uint8_t> payload);
};

} // namespace forge::p2p
