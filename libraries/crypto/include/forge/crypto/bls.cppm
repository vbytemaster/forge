module;
#include <forge/exceptions/macros.hpp>
#include <array>
#include <bls12-381/bls12-381.hpp>
#include <boost/describe.hpp>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <span>
#include <string>

export module forge.crypto.bls;

import forge.crypto.base64;
import forge.crypto.ripemd160;
import forge.exceptions;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;
import forge.reflect.reflect;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.variant.described;

namespace forge::crypto::bls::detail {

template <typename DataType> struct checked_data {
   std::uint32_t check = 0;
   DataType data;

   static auto calculate_checksum(const DataType& data) {
      auto encoder = forge::crypto::ripemd160::encoder();
      raw::pack(encoder, data);
      return encoder.result()._hash[0];
   }

   template <typename Stream> friend Stream& operator<<(Stream& s, const checked_data& value) {
      forge::raw::pack(s, value.data);
      forge::raw::pack(s, value.check);
      return s;
   }

   template <typename Stream> friend Stream& operator>>(Stream& s, checked_data& value) {
      forge::raw::unpack(s, value.data);
      forge::raw::unpack(s, value.check);
      return s;
   }
};

template <typename Container> Container deserialize_base64url(const std::string& data_str) {
   using wrapper = checked_data<Container>;
   auto wrapped = wrapper{};

   auto bin = forge::crypto::base64url_decode(data_str);
   forge::datastream<const char*> unpacker(bin.data(), bin.size());
   forge::raw::unpack(unpacker, wrapped);
   FORGE_ASSERT(!unpacker.remaining(), "decoded base64url length too long");
   FORGE_ASSERT(wrapper::calculate_checksum(wrapped.data) == wrapped.check);

   return wrapped.data;
}

template <typename Container> std::string serialize_base64url(const Container& data) {
   using wrapper = checked_data<Container>;
   auto wrapped = wrapper{};

   wrapped.data = data;
   wrapped.check = wrapper::calculate_checksum(wrapped.data);
   auto packed = raw::pack(wrapped);
   return forge::crypto::base64url_encode(packed.data(), packed.size());
}

} // namespace forge::crypto::bls::detail

export namespace forge::crypto::bls {

namespace config {
const std::string public_key_prefix = "PUB_BLS_";
const std::string private_key_prefix = "PVT_BLS_";
const std::string signature_prefix = "SIG_BLS_";
} // namespace config

class signature;

class public_key {
 public:
   public_key() = default;
   public_key(public_key&&) = default;
   public_key(const public_key&) = default;
   public_key& operator=(const public_key& rhs) = default;
   public_key& operator=(public_key&& rhs) = default;

   explicit public_key(std::span<const std::uint8_t, 96> affine_non_montgomery_le);
   explicit public_key(const std::string& base64urlstr);

   [[nodiscard]] std::string to_string() const;

   [[nodiscard]] const bls12_381::g1& jacobian_montgomery_le() const {
      return _jacobian_montgomery_le;
   }
   [[nodiscard]] const std::array<std::uint8_t, 96>& affine_non_montgomery_le() const {
      return _affine_non_montgomery_le;
   }

   [[nodiscard]] bool equal(const public_key& pkey) const {
      return _jacobian_montgomery_le.equal(pkey._jacobian_montgomery_le);
   }

   auto operator<=>(const public_key& rhs) const {
      return _affine_non_montgomery_le <=> rhs._affine_non_montgomery_le;
   }
   auto operator==(const public_key& rhs) const {
      return _affine_non_montgomery_le == rhs._affine_non_montgomery_le;
   }

   template <typename T> friend T& operator<<(T& ds, const public_key& key) {
      forge::raw::pack(ds, forge::unsigned_int(static_cast<std::uint32_t>(sizeof(key._affine_non_montgomery_le))));
      ds.write(reinterpret_cast<const char*>(key._affine_non_montgomery_le.data()),
               sizeof(key._affine_non_montgomery_le));
      return ds;
   }

   friend std::ostream& operator<<(std::ostream& os, const public_key& key) {
      os << "bls::public_key(0x" << std::hex;
      for (auto c : key.affine_non_montgomery_le())
         os << std::setfill('0') << std::setw(2) << static_cast<int>(c);
      os << std::dec << ")";
      return os;
   }

   template <typename T> friend T& operator>>(T& ds, public_key& key) {
      forge::unsigned_int size;
      forge::raw::unpack(ds, size);
      FORGE_ASSERT(size.value == sizeof(key._affine_non_montgomery_le));
      ds.read(reinterpret_cast<char*>(key._affine_non_montgomery_le.data()), sizeof(key._affine_non_montgomery_le));
      key._jacobian_montgomery_le = from_affine_bytes_le(key._affine_non_montgomery_le);
      return ds;
   }

   [[nodiscard]] static bls12_381::g1 from_affine_bytes_le(
      const std::array<std::uint8_t, 96>& affine_non_montgomery_le);

 private:
   std::array<std::uint8_t, 96> _affine_non_montgomery_le{};
   bls12_381::g1 _jacobian_montgomery_le;
};

class private_key {
 public:
   private_key() = default;
   private_key(private_key&&) = default;
   private_key(const private_key&) = default;
   explicit private_key(std::span<const std::uint8_t> seed) {
      _sk = bls12_381::secret_key(seed);
   }
   explicit private_key(const std::string& base64urlstr);

   private_key& operator=(const private_key&) = default;

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] public_key get_public_key() const;
   [[nodiscard]] signature sign(std::span<const std::uint8_t> msg) const;
   [[nodiscard]] signature proof_of_possession() const;
   [[nodiscard]] static private_key generate();

 private:
   std::array<std::uint64_t, 4> _sk{};
   BOOST_DESCRIBE_CLASS(private_key, (), (), (), (_sk))
   friend bool operator==(const private_key& left, const private_key& right);
};

class signature {
 public:
   signature() = default;
   signature(signature&&) = default;
   signature(const signature&) = default;
   signature& operator=(const signature&) = default;
   signature& operator=(signature&&) = default;

   explicit signature(std::span<const std::uint8_t, 192> affine_non_montgomery_le);
   explicit signature(const std::string& base64urlstr);

   [[nodiscard]] std::string to_string() const;

   [[nodiscard]] const bls12_381::g2& jacobian_montgomery_le() const {
      return _jacobian_montgomery_le;
   }
   [[nodiscard]] const std::array<std::uint8_t, 192>& affine_non_montgomery_le() const {
      return _affine_non_montgomery_le;
   }

   [[nodiscard]] bool equal(const signature& sig) const {
      return _jacobian_montgomery_le.equal(sig._jacobian_montgomery_le);
   }

   auto operator<=>(const signature& rhs) const {
      return _affine_non_montgomery_le <=> rhs._affine_non_montgomery_le;
   }
   auto operator==(const signature& rhs) const {
      return _affine_non_montgomery_le == rhs._affine_non_montgomery_le;
   }

   template <typename T> friend T& operator<<(T& ds, const signature& sig) {
      forge::raw::pack(ds, forge::unsigned_int(static_cast<std::uint32_t>(sizeof(sig._affine_non_montgomery_le))));
      ds.write(reinterpret_cast<const char*>(sig._affine_non_montgomery_le.data()),
               sizeof(sig._affine_non_montgomery_le));
      return ds;
   }

   template <typename T> friend T& operator>>(T& ds, signature& sig) {
      forge::unsigned_int size;
      forge::raw::unpack(ds, size);
      FORGE_ASSERT(size.value == sizeof(sig._affine_non_montgomery_le));
      ds.read(reinterpret_cast<char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
      sig._jacobian_montgomery_le = to_jacobian_montgomery_le(sig._affine_non_montgomery_le);
      return ds;
   }

   [[nodiscard]] static bls12_381::g2 to_jacobian_montgomery_le(
      const std::array<std::uint8_t, 192>& affine_non_montgomery_le);

 private:
   std::array<std::uint8_t, 192> _affine_non_montgomery_le{};
   bls12_381::g2 _jacobian_montgomery_le;
};

class aggregate_signature {
 public:
   aggregate_signature() = default;
   aggregate_signature(aggregate_signature&&) = default;
   aggregate_signature(const aggregate_signature&) = default;
   aggregate_signature& operator=(const aggregate_signature&) = default;
   aggregate_signature& operator=(aggregate_signature&&) = default;

   explicit aggregate_signature(const std::string& base64urlstr);
   explicit aggregate_signature(const signature& sig) : _jacobian_montgomery_le(sig.jacobian_montgomery_le()) {}

   void aggregate(const signature& sig) {
      _jacobian_montgomery_le.addAssign(sig.jacobian_montgomery_le());
   }
   void aggregate(const aggregate_signature& sig) {
      _jacobian_montgomery_le.addAssign(sig.jacobian_montgomery_le());
   }

   [[nodiscard]] std::string to_string() const;
   [[nodiscard]] const bls12_381::g2& jacobian_montgomery_le() const {
      return _jacobian_montgomery_le;
   }
   [[nodiscard]] bool equal(const aggregate_signature& sig) const {
      return _jacobian_montgomery_le.equal(sig._jacobian_montgomery_le);
   }

   template <typename T> friend T& operator<<(T& ds, const aggregate_signature& sig) {
      std::array<std::uint8_t, 192> affine_non_montgomery_le =
         sig._jacobian_montgomery_le.toAffineBytesLE(bls12_381::from_mont::yes);
      forge::raw::pack(ds, forge::unsigned_int(static_cast<std::uint32_t>(sizeof(affine_non_montgomery_le))));
      ds.write(reinterpret_cast<const char*>(affine_non_montgomery_le.data()), sizeof(affine_non_montgomery_le));
      return ds;
   }

   template <typename T> friend T& operator>>(T& ds, aggregate_signature& sig) {
      forge::unsigned_int size;
      forge::raw::unpack(ds, size);
      auto affine_non_montgomery_le = std::array<std::uint8_t, 192>{};
      FORGE_ASSERT(size.value == sizeof(affine_non_montgomery_le));
      ds.read(reinterpret_cast<char*>(affine_non_montgomery_le.data()), sizeof(affine_non_montgomery_le));
      sig._jacobian_montgomery_le = signature::to_jacobian_montgomery_le(affine_non_montgomery_le);
      return ds;
   }

 private:
   bls12_381::g2 _jacobian_montgomery_le;
};

[[nodiscard]] bool verify(const public_key& pubkey, std::span<const std::uint8_t> message, const signature& sig);

void to_variant(const public_key& var, variant& vo);
void from_variant(const variant& var, public_key& vo);
void to_variant(const private_key& var, variant& vo);
void from_variant(const variant& var, private_key& vo);
void to_variant(const signature& var, variant& vo);
void from_variant(const variant& var, signature& vo);
void to_variant(const aggregate_signature& var, variant& vo);
void from_variant(const variant& var, aggregate_signature& vo);

} // namespace forge::crypto::bls
