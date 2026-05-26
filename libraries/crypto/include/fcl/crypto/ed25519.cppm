module;
#include <array>
#include <boost/describe.hpp>
#include <cstdint>
#include <span>

export module fcl.crypto.ed25519;

import fcl.crypto.common;
import fcl.crypto.types;

export namespace fcl::crypto::ed25519 {

using public_key_data = std::array<std::uint8_t, 32>;
using private_key_secret = std::array<std::uint8_t, 32>;
using signature_data = std::array<std::uint8_t, 64>;

class public_key {
 public:
   public_key() = default;
   explicit public_key(const public_key_data& value);

   [[nodiscard]] const public_key_data& serialize() const noexcept;
   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature_data& signature) const;

   friend bool operator==(const public_key&, const public_key&) = default;

 private:
   public_key_data data_{};
};

class private_key {
 public:
   private_key() = default;
   explicit private_key(const private_key_secret& value);

   [[nodiscard]] static private_key generate();
   [[nodiscard]] static private_key regenerate(const private_key_secret& value);

   [[nodiscard]] const private_key_secret& get_secret() const noexcept;
   [[nodiscard]] public_key get_public_key() const;
   [[nodiscard]] signature_data sign(std::span<const std::uint8_t> message) const;

   friend bool operator==(const private_key&, const private_key&) = default;

 private:
   private_key_secret data_{};
};

struct signature_shim;

struct public_key_shim : public fcl::crypto::shim<public_key_data> {
   using signature_type = signature_shim;
   using fcl::crypto::shim<public_key_data>::shim;

   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature_data& signature) const;
};

struct signature_shim : public fcl::crypto::shim<signature_data> {
   using public_key_type = public_key_shim;
   using fcl::crypto::shim<signature_data>::shim;
};

struct private_key_shim : public fcl::crypto::shim<private_key_secret> {
   using signature_type = signature_shim;
   using public_key_type = public_key_shim;
   using fcl::crypto::shim<private_key_secret>::shim;

   [[nodiscard]] signature_type sign(std::span<const std::uint8_t> message) const;
   [[nodiscard]] public_key_type get_public_key() const;
   [[nodiscard]] static private_key_shim generate();
};

} // namespace fcl::crypto::ed25519

export namespace fcl::crypto::ed25519 {
BOOST_DESCRIBE_STRUCT(public_key_shim, (fcl::crypto::shim<public_key_data>), ())
BOOST_DESCRIBE_STRUCT(signature_shim, (fcl::crypto::shim<signature_data>), ())
BOOST_DESCRIBE_STRUCT(private_key_shim, (fcl::crypto::shim<private_key_secret>), ())
} // namespace fcl::crypto::ed25519
