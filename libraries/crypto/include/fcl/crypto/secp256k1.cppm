module;
#include <array>
#include <boost/describe.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

export module fcl.crypto.secp256k1;

import fcl.crypto.bigint;
import fcl.crypto.common;
import fcl.crypto.openssl;
import fcl.crypto.sha256;
import fcl.crypto.sha512;
import fcl.raw.raw;

export namespace fcl::crypto::secp256k1 {
namespace detail {
class public_key_impl;
class private_key_impl;
} // namespace detail

typedef fcl::crypto::sha256 blind_factor_type;
typedef std::array<char, 33> commitment_type;
typedef std::array<char, 33> public_key_data;
typedef fcl::crypto::sha256 private_key_secret;
typedef std::array<char, 65> public_key_point_data; ///< the full uncompressed secp256k1 point
typedef std::array<char, 72> signature;
typedef std::array<unsigned char, 65> compact_signature;
typedef std::array<char, 78> extended_key_data;
typedef fcl::crypto::sha256 blinded_hash;
typedef fcl::crypto::sha256 blind_signature;

/**
 *  @class public_key
 *  @brief contains only the public point of a secp256k1 key.
 */
class public_key {
 public:
   public_key();
   public_key(const public_key& k);
   ~public_key();
   //           bool verify( const fcl::crypto::sha256& digest, const signature& sig );
   public_key_data serialize() const;
   public_key_point_data serialize_ecc_point() const;

   operator public_key_data() const {
      return serialize();
   }

   public_key(const public_key_data& v);
   public_key(const public_key_point_data& v);
   public_key(const compact_signature& c, const fcl::crypto::sha256& digest, bool check_canonical = true);

   bool valid() const;

   public_key(public_key&& pk);
   public_key& operator=(public_key&& pk);
   public_key& operator=(const public_key& pk);

   inline friend bool operator==(const public_key& a, const public_key& b) {
      return a.serialize() == b.serialize();
   }
   inline friend bool operator!=(const public_key& a, const public_key& b) {
      return a.serialize() != b.serialize();
   }

   unsigned int fingerprint() const;

 private:
   friend class private_key;
   static public_key from_key_data(const public_key_data& v);
   static bool is_canonical(const compact_signature& c);
   std::unique_ptr<detail::public_key_impl> my;
};

/**
 *  @class private_key
 *  @brief a secp256k1 private key.
 */
class private_key {
 public:
   private_key();
   private_key(private_key&& pk);
   private_key(const private_key& pk);
   ~private_key();

   private_key& operator=(private_key&& pk);
   private_key& operator=(const private_key& pk);

   static private_key generate();
   static private_key regenerate(const fcl::crypto::sha256& secret);

   private_key child(const fcl::crypto::sha256& offset) const;

   /**
    *  This method of generation enables creating a new private key in a deterministic manner relative to
    *  an initial seed.   A public_key created from the seed can be multiplied by the offset to calculate
    *  the new public key without having to know the private key.
    */
   static private_key generate_from_seed(const fcl::crypto::sha256& seed, const fcl::crypto::sha256& offset = fcl::crypto::sha256());

   private_key_secret get_secret() const; // get the private key secret

   operator private_key_secret() const {
      return get_secret();
   }

   /**
    *  Given a public key, calculatse a 512 bit shared secret between that
    *  key and this private key.
    */
   fcl::crypto::sha512 get_shared_secret(const public_key& pub) const;

   //           signature         sign( const fcl::crypto::sha256& digest )const;
   compact_signature sign_digest(const fcl::crypto::sha256& digest, bool require_canonical = true) const {
      return sign_compact(digest, require_canonical);
   }
   compact_signature sign_compact(const fcl::crypto::sha256& digest, bool require_canonical = true) const;
   //           bool              verify( const fcl::crypto::sha256& digest, const signature& sig );

   public_key get_public_key() const;

   inline friend bool operator==(const private_key& a, const private_key& b) {
      return a.get_secret() == b.get_secret();
   }
   inline friend std::strong_ordering operator<=>(const private_key& a, const private_key& b) {
      return a.get_secret() <=> b.get_secret();
   }

   unsigned int fingerprint() const {
      return get_public_key().fingerprint();
   }

 private:
   std::unique_ptr<detail::private_key_impl> my;
};

/**
 * Shims
 */
struct signature_shim;

struct public_key_shim : public crypto::shim<public_key_data> {
   using signature_type = signature_shim;
   using crypto::shim<public_key_data>::shim;

   bool valid() const {
      return public_key(_data).valid();
   }
};

struct signature_shim : public crypto::shim<compact_signature> {
   using public_key_type = public_key_shim;
   using crypto::shim<compact_signature>::shim;

   public_key_type recover(const sha256& digest, bool check_canonical) const {
      return public_key_type(public_key(_data, digest, check_canonical).serialize());
   }
};

struct private_key_shim : public crypto::shim<private_key_secret> {
   using crypto::shim<private_key_secret>::shim;
   using signature_type = signature_shim;
   using public_key_type = public_key_shim;

   signature_type sign(const sha256& digest, bool require_canonical = true) const {
      return signature_type(private_key::regenerate(_data).sign_compact(digest, require_canonical));
   }

   signature_type sign(std::span<const std::uint8_t> message) const {
      return sign(sha256::hash(message), true);
   }

   public_key_type get_public_key() const {
      return public_key_type(private_key::regenerate(_data).get_public_key().serialize());
   }

   sha512 get_shared_secret(const public_key_type& pub_key) const {
      return private_key::regenerate(_data).get_shared_secret(public_key(pub_key.serialize()));
   }

   static private_key_shim generate() {
      return private_key_shim(private_key::generate().get_secret());
   }
};

inline bool verify_digest(const public_key_shim& key, const sha256& digest, const signature_shim& signature,
                          bool check_canonical = true) {
   return public_key(signature.serialize(), digest, check_canonical).serialize() == key.serialize();
}

inline bool verify_message(const public_key_shim& key, std::span<const std::uint8_t> message,
                           const signature_shim& signature) {
   return verify_digest(key, sha256::hash(message), signature, true);
}

enum class recover_error : std::int32_t {
   init_error,
   input_error,
   invalid_signature,
   recover_error,
};

using recover_bytes = std::vector<char>;

std::variant<recover_error, recover_bytes> recover(const recover_bytes& signature, const recover_bytes& digest);

BOOST_DESCRIBE_STRUCT(public_key_shim, (fcl::crypto::shim<public_key_data>), ())
BOOST_DESCRIBE_STRUCT(signature_shim, (fcl::crypto::shim<compact_signature>), ())
BOOST_DESCRIBE_STRUCT(private_key_shim, (fcl::crypto::shim<private_key_secret>), ())
} // namespace fcl::crypto::secp256k1
