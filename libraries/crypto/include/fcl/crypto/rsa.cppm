module;
#include <boost/describe.hpp>
#include <cstdint>
#include <span>
#include <vector>

export module fcl.crypto.rsa;

import fcl.crypto.common;
import fcl.crypto.types;

export namespace fcl::crypto::rsa {

using public_key_data = bytes;
using private_key_secret = bytes;
using signature_data = bytes;

class public_key {
 public:
   public_key() = default;
   explicit public_key(public_key_data value);

   [[nodiscard]] const public_key_data& serialize() const noexcept;
   [[nodiscard]] bool valid() const noexcept;
   [[nodiscard]] bool verify(std::span<const std::uint8_t> message, const signature_data& signature) const;

   friend bool operator==(const public_key&, const public_key&) = default;

 private:
   public_key_data data_;
};

class private_key {
 public:
   private_key() = default;
   explicit private_key(private_key_secret value);

   [[nodiscard]] static private_key generate(std::uint32_t bits = 2048);
   [[nodiscard]] static private_key regenerate(private_key_secret value);

   [[nodiscard]] const private_key_secret& get_secret() const noexcept;
   [[nodiscard]] public_key get_public_key() const;
   [[nodiscard]] signature_data sign(std::span<const std::uint8_t> message) const;

   friend bool operator==(const private_key&, const private_key&) = default;

 private:
   private_key_secret data_;
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

} // namespace fcl::crypto::rsa

export namespace fcl::crypto::rsa {
BOOST_DESCRIBE_STRUCT(public_key_shim, (fcl::crypto::shim<public_key_data>), ())
BOOST_DESCRIBE_STRUCT(signature_shim, (fcl::crypto::shim<signature_data>), ())
BOOST_DESCRIBE_STRUCT(private_key_shim, (fcl::crypto::shim<private_key_secret>), ())
} // namespace fcl::crypto::rsa
