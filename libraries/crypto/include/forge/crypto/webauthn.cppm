module;
#include <forge/exceptions/macros.hpp>
#include <array>
#include <string>
#include <tuple>
#include <vector>
#include <boost/describe.hpp>

export module forge.crypto.webauthn;

import forge.crypto.sha256;
import forge.crypto.p256;
export import forge.exceptions;
import forge.raw.raw;

export namespace forge::crypto::webauthn {

namespace exceptions {

enum class code : std::uint16_t {
   invalid_client_data = 1,
   invalid_signature = 2,
   invalid_options = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.webauthn")

using invalid_client_data = forge::exceptions::coded_exception<code, code::invalid_client_data>;
using invalid_signature = forge::exceptions::coded_exception<code, code::invalid_signature>;
using invalid_options = forge::exceptions::coded_exception<code, code::invalid_options>;

} // namespace exceptions

class assertion;

class credential_public_key {
 public:
   using public_key_data_type = std::array<char, 33>;

   // Used for base58 de/serialization
   using data_type = credential_public_key;
   credential_public_key serialize() const {
      return *this;
   }

   enum class user_presence_t : uint8_t { USER_PRESENCE_NONE, USER_PRESENCE_PRESENT, USER_PRESENCE_VERIFIED };

   bool valid() const {
      return rpid.size();
   }

   credential_public_key() {}
   credential_public_key(const public_key_data_type& p, const user_presence_t& t, const std::string& s)
       : public_key_data(p), user_verification_type(t), rpid(s) {
      post_init();
   }
   credential_public_key(const assertion& c, const forge::crypto::sha256& digest, bool check_canonical = true);

   bool operator==(const credential_public_key& o) const {
      return public_key_data == o.public_key_data && user_verification_type == o.user_verification_type &&
             rpid == o.rpid;
   }
   bool operator<(const credential_public_key& o) const {
      return std::tie(public_key_data, user_verification_type, rpid) <
             std::tie(o.public_key_data, o.user_verification_type, o.rpid);
   }

   template <typename Stream> friend Stream& operator<<(Stream& ds, const credential_public_key& k) {
      forge::raw::pack(ds, k.public_key_data);
      forge::raw::pack(ds, static_cast<uint8_t>(k.user_verification_type));
      forge::raw::pack(ds, k.rpid);
      return ds;
   }

   template <typename Stream> friend Stream& operator>>(Stream& ds, credential_public_key& k) {
      forge::raw::unpack(ds, k.public_key_data);
      uint8_t t;
      forge::raw::unpack(ds, t);
      k.user_verification_type = static_cast<user_presence_t>(t);
      forge::raw::unpack(ds, k.rpid);
      k.post_init();
      return ds;
   }

 private:
   public_key_data_type public_key_data;
   user_presence_t user_verification_type = user_presence_t::USER_PRESENCE_NONE;
   std::string rpid;

   void post_init();
};

class assertion {
 public:
   // used for base58 de/serialization
   using data_type = assertion;
   assertion serialize() const {
      return *this;
   }

   assertion() {}
   assertion(const forge::crypto::p256::compact_signature& s, const std::vector<uint8_t>& a, const std::string& j)
       : compact_signature(s), auth_data(a), client_json(j) {}

   credential_public_key recover(const sha256& digest, bool check_canonical) const {
      return credential_public_key(*this, digest, check_canonical);
   }

   size_t variable_size() const {
      return auth_data.size() + client_json.size();
   }

   bool operator==(const assertion& o) const {
      return compact_signature == o.compact_signature && auth_data == o.auth_data && client_json == o.client_json;
   }

   bool operator<(const assertion& o) const {
      return std::tie(compact_signature, auth_data, client_json) <
             std::tie(o.compact_signature, o.auth_data, o.client_json);
   }

   // for container usage
   size_t get_hash() const {
      return *(size_t*)&compact_signature.data()[32 - sizeof(size_t)] +
             *(size_t*)&compact_signature.data()[64 - sizeof(size_t)];
   }

   BOOST_DESCRIBE_CLASS(assertion, (), (), (), (compact_signature, auth_data, client_json))
   friend class credential_public_key;

 private:
   forge::crypto::p256::compact_signature compact_signature;
   std::vector<uint8_t> auth_data;
   std::string client_json;
};

} // namespace forge::crypto::webauthn
