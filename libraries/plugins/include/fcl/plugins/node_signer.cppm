module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <fcl/exceptions/macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <typeinfo>
#include <vector>

export module fcl.plugins.node_signer;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.crypto.asymmetric;
import fcl.crypto.sha256;
import fcl.exceptions;
import fcl.schema;

export namespace fcl::plugins {

class node_signer final : public fcl::app::plugin {
 public:
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

      [[nodiscard]] std::string signature_text() const;
   };

   class exceptions;
   class api;

   explicit node_signer(plugin_options value = {});
   ~node_signer() override;

   node_signer(const node_signer&) = delete;
   node_signer& operator=(const node_signer&) = delete;

   [[nodiscard]] static fcl::app::plugin_descriptor descriptor(plugin_options value = {});

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class api_impl;
   std::shared_ptr<impl> impl_;
};

std::ostream& operator<<(std::ostream& out, node_signer::key_algorithm value);

class node_signer::exceptions {
 public:
   enum class code : std::uint16_t {
      invalid_config = 1,
      key_not_found = 2,
      purpose_denied = 3,
      unsupported_algorithm = 4,
      unsupported_profile = 5,
      invalid_key = 6,
      signing_failed = 7,
   };

   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using key_not_found = fcl::exceptions::coded_exception<code, code::key_not_found>;
   using purpose_denied = fcl::exceptions::coded_exception<code, code::purpose_denied>;
   using unsupported_algorithm = fcl::exceptions::coded_exception<code, code::unsupported_algorithm>;
   using unsupported_profile = fcl::exceptions::coded_exception<code, code::unsupported_profile>;
   using invalid_key = fcl::exceptions::coded_exception<code, code::invalid_key>;
   using signing_failed = fcl::exceptions::coded_exception<code, code::signing_failed>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(node_signer::exceptions::code, "fcl.plugins.node_signer")

class node_signer::api : public fcl::api::contract<node_signer::api, fcl::api::surface::local> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<response> sign(request value) = 0;
   boost::asio::awaitable<response> sign(std::string key_id, std::string purpose, fcl::crypto::sha256 digest);
   boost::asio::awaitable<response> sign(std::string key_id, fcl::crypto::sha256 digest, options value);
};

BOOST_DESCRIBE_ENUM(node_signer::key_algorithm, any, secp256k1, p256, ed25519, rsa)
BOOST_DESCRIBE_STRUCT(node_signer::key, (), (id, private_key, input_profile, purposes))
BOOST_DESCRIBE_STRUCT(node_signer::config, (), (keys, default_output_profile))
BOOST_DESCRIBE_STRUCT(node_signer::request, (), (key_id, purpose, digest, required_algorithm, output_profile))
BOOST_DESCRIBE_STRUCT(node_signer::options, (), (purpose, required_algorithm, output_profile))
BOOST_DESCRIBE_STRUCT(node_signer::response, (), (key_id, algorithm, output_profile, public_key, signature))

} // namespace fcl::plugins

export namespace fcl::api {

template <> struct api_traits<::fcl::plugins::node_signer::api> {
   using interface = ::fcl::plugins::node_signer::api;
   using request = ::fcl::plugins::node_signer::request;
   using response = ::fcl::plugins::node_signer::response;
   using sign_method = boost::asio::awaitable<response> (interface::*)(request);

   static api_id id() {
      return api_id{.value = "fcl.plugins.node_signer"};
   }

   static api_version version() {
      return api_version{.major = 1, .revision = 0};
   }

   static api_ref ref(std::uint16_t min_revision = version().revision) {
      const auto value = version();
      return api_ref{.id = id(), .major = value.major, .min_revision = min_revision};
   }

   static descriptor describe() {
      auto builder = ::fcl::api::define<interface>(
         descriptor{.id = id(), .version = version(), .interface_type = typeid(interface)});
      (void)builder.template method<static_cast<sign_method>(&interface::sign)>("sign");
      return builder.build();
   }
};

} // namespace fcl::api
