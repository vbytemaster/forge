module;

#include <boost/describe.hpp>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

export module forge.plugins.crypto.signer.types;

import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

export namespace forge::plugins::crypto::signer {

enum class key_algorithm {
   any,
   secp256k1,
   p256,
   ed25519,
   rsa,
};

struct plugin_options {
   std::vector<forge::crypto::asymmetric::text_encoding_profile> profiles;
};

struct key {
   std::string id;
   std::string private_key;
   std::string input_profile = "forge";
   std::vector<std::string> purposes;
};

struct config {
   std::vector<key> keys;
   std::string default_output_profile = "forge";
};

struct request {
   std::string key_id;
   std::string purpose;
   forge::crypto::sha256 digest;
   key_algorithm required_algorithm = key_algorithm::any;
   std::string output_profile;
};

struct options {
   std::string purpose;
   key_algorithm required_algorithm = key_algorithm::any;
   std::string output_profile;
};

struct response {
   std::string key_id;
   key_algorithm algorithm = key_algorithm::any;
   std::string output_profile;
   std::string public_key;
   std::vector<std::uint8_t> signature;

   [[nodiscard]] std::string signature_text() const {
      return std::string{signature.begin(), signature.end()};
   }
};

inline std::ostream& operator<<(std::ostream& out, key_algorithm value) {
   switch (value) {
   case key_algorithm::any:
      return out << "any";
   case key_algorithm::secp256k1:
      return out << "secp256k1";
   case key_algorithm::p256:
      return out << "p256";
   case key_algorithm::ed25519:
      return out << "ed25519";
   case key_algorithm::rsa:
      return out << "rsa";
   }
   return out << "unknown";
}

BOOST_DESCRIBE_ENUM(key_algorithm, any, secp256k1, p256, ed25519, rsa)
BOOST_DESCRIBE_STRUCT(key, (), (id, private_key, input_profile, purposes))
BOOST_DESCRIBE_STRUCT(config, (), (keys, default_output_profile))
BOOST_DESCRIBE_STRUCT(request, (), (key_id, purpose, digest, required_algorithm, output_profile))
BOOST_DESCRIBE_STRUCT(options, (), (purpose, required_algorithm, output_profile))
BOOST_DESCRIBE_STRUCT(response, (), (key_id, algorithm, output_profile, public_key, signature))

} // namespace forge::plugins::crypto::signer

export template <> struct forge::schema::rules<forge::plugins::crypto::signer::key> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::crypto::signer::key> define() {
      auto schema = forge::schema::object<forge::plugins::crypto::signer::key>();
      schema.field<&forge::plugins::crypto::signer::key::id>("id").required().non_empty();
      schema.field<&forge::plugins::crypto::signer::key::private_key>("private-key")
         .required()
         .non_empty()
         .secret()
         .description("Private key material in one of the configured input profiles");
      schema.field<&forge::plugins::crypto::signer::key::input_profile>("input-profile").default_value("forge");
      schema.field<&forge::plugins::crypto::signer::key::purposes>("purposes")
         .min_items(1)
         .each_non_empty()
         .description("Allowed signature purposes for this key");
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::crypto::signer::config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::crypto::signer::config> define() {
      auto schema = forge::schema::object<forge::plugins::crypto::signer::config>();
      schema.field<&forge::plugins::crypto::signer::config::keys>("keys")
         .items<forge::plugins::crypto::signer::key>()
         .secret()
         .unique_by<&forge::plugins::crypto::signer::key::id>()
         .description("Local crypto signer key entries");
      schema.field<&forge::plugins::crypto::signer::config::default_output_profile>("default-output-profile")
         .default_value("forge")
         .description("Default signature text encoding profile");
      return schema;
   }
};
