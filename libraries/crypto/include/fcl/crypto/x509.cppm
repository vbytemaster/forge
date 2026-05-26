module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module fcl.crypto.x509;

import fcl.crypto.public_key;
import fcl.crypto.types;

export namespace fcl::crypto::x509 {

class certificate {
 public:
   certificate() = default;
   explicit certificate(bytes der);

   [[nodiscard]] static certificate from_der(std::span<const std::uint8_t> bytes);
   [[nodiscard]] static certificate from_pem(std::string_view text);

   [[nodiscard]] const bytes& der() const noexcept;
   [[nodiscard]] bytes public_key_der() const;
   [[nodiscard]] fcl::crypto::public_key key() const;
   [[nodiscard]] bytes extension(std::string_view oid) const;
   [[nodiscard]] bytes fingerprint_sha256() const;
   [[nodiscard]] std::string fingerprint_sha256_text() const;

 private:
   bytes der_;
};

} // namespace fcl::crypto::x509
