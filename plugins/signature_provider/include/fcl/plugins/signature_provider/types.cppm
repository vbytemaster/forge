module;

#include <boost/describe.hpp>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

export module fcl.plugins.signature_provider.types;

import fcl.crypto.asymmetric;
import fcl.crypto.sha256;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::plugins::signature_provider {

enum class key_algorithm {
   any,
   secp256k1,
   p256,
   ed25519,
   rsa,
};

struct plugin_options {
   std::vector<fcl::crypto::asymmetric::text_encoding_profile> profiles;
};

struct key {
   std::string id;
   std::string private_key;
   std::string input_profile = "fcl";
   std::vector<std::string> purposes;
};

struct config {
   std::vector<key> keys;
   std::string default_output_profile = "fcl";
};

struct request {
   std::string key_id;
   std::string purpose;
   fcl::crypto::sha256 digest;
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

} // namespace fcl::plugins::signature_provider

export template <> struct fcl::schema::rules<fcl::plugins::signature_provider::key> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::signature_provider::key> define() {
      auto schema = fcl::schema::object<fcl::plugins::signature_provider::key>();
      schema.field<&fcl::plugins::signature_provider::key::id>("id").required().non_empty();
      schema.field<&fcl::plugins::signature_provider::key::private_key>("private-key")
         .required()
         .secret()
         .description("Private key material in one of the configured input profiles");
      schema.field<&fcl::plugins::signature_provider::key::input_profile>("input-profile").default_value("fcl");
      schema.field<&fcl::plugins::signature_provider::key::purposes>("purposes")
         .min_items(1)
         .each_non_empty()
         .description("Allowed signature purposes for this key");
      return schema;
   }
};

export template <> struct fcl::schema::rules<fcl::plugins::signature_provider::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::signature_provider::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::signature_provider::config>();
      schema.field<&fcl::plugins::signature_provider::config::keys>("keys")
         .items<fcl::plugins::signature_provider::key>()
         .secret()
         .unique_by<&fcl::plugins::signature_provider::key::id>()
         .description("Local signature provider key entries");
      schema.field<&fcl::plugins::signature_provider::config::default_output_profile>("default-output-profile")
         .default_value("fcl")
         .description("Default signature text encoding profile");
      return schema;
   }
};
