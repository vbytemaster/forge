module;

#include <fcl/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <coroutine>
#include <exception>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module fcl.plugins.signature_provider.plugin;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.config.document;
import fcl.config.value;
import fcl.crypto.asymmetric;
import fcl.crypto.ed25519;
import fcl.crypto.p256;
import fcl.crypto.rsa;
import fcl.crypto.secp256k1;
import fcl.crypto.sha256;
import fcl.exceptions;
import fcl.plugins.signature_provider.api;
import fcl.plugins.signature_provider.exceptions;
import fcl.plugins.signature_provider.types;
import fcl.schema;

namespace fcl::plugins::signature_provider {
namespace {

[[nodiscard]] key_algorithm to_key_algorithm(fcl::crypto::asymmetric::algorithm value) noexcept {
   using asymmetric_algorithm = fcl::crypto::asymmetric::algorithm;
   switch (value) {
   case asymmetric_algorithm::secp256k1:
      return key_algorithm::secp256k1;
   case asymmetric_algorithm::p256:
      return key_algorithm::p256;
   case asymmetric_algorithm::ed25519:
      return key_algorithm::ed25519;
   case asymmetric_algorithm::rsa:
      return key_algorithm::rsa;
   }
   return key_algorithm::any;
}

[[nodiscard]] std::string require_string(const fcl::config::value::object_type& object, std::string_view key,
                                         std::string fallback = {}) {
   const auto found = object.find(std::string{key});
   if (found == object.end()) {
      return fallback;
   }
   const auto* value = std::get_if<std::string>(&found->second.storage);
   if (value == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature provider config field must be a string",
                          fcl::exceptions::ctx("field", std::string{key}));
   }
   return *value;
}

[[nodiscard]] std::vector<std::string> optional_string_list(const fcl::config::value::object_type& object,
                                                           std::string_view key) {
   const auto found = object.find(std::string{key});
   if (found == object.end()) {
      return {};
   }
   const auto* values = std::get_if<fcl::config::value::array_type>(&found->second.storage);
   if (values == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature provider config field must be a string list",
                          fcl::exceptions::ctx("field", std::string{key}));
   }
   auto result = std::vector<std::string>{};
   result.reserve(values->size());
   for (const auto& item : *values) {
      const auto* text = std::get_if<std::string>(&item.storage);
      if (text == nullptr) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature provider config list item must be a string",
                             fcl::exceptions::ctx("field", std::string{key}));
      }
      result.push_back(*text);
   }
   return result;
}

[[nodiscard]] config decode_config(const fcl::config::component_view& view) {
   auto result = config{};
   result.default_output_profile = view.get_or<std::string>("default-output-profile", "fcl");

   const auto* keys_value = view.try_get("keys");
   if (keys_value == nullptr) {
      return result;
   }
   const auto* keys = std::get_if<fcl::config::value::array_type>(&keys_value->storage);
   if (keys == nullptr) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider keys must be an array");
   }

   auto ids = std::set<std::string>{};
   result.keys.reserve(keys->size());
   for (const auto& item : *keys) {
      const auto* object = std::get_if<fcl::config::value::object_type>(&item.storage);
      if (object == nullptr) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key entry must be an object");
      }
      auto entry = key{
         .id = require_string(*object, "id"),
         .private_key = require_string(*object, "private-key"),
         .input_profile = require_string(*object, "input-profile", "fcl"),
         .purposes = optional_string_list(*object, "purposes"),
      };
      if (entry.id.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key id is required");
      }
      if (entry.private_key.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider private-key is required");
      }
      if (entry.purposes.empty()) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature-provider key purposes must be explicitly configured",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      if (std::ranges::any_of(entry.purposes, [](const auto& purpose) {
             return purpose.empty();
          })) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config,
                             "signature-provider key purpose must not be empty",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      if (!ids.insert(entry.id).second) {
         FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature-provider key id is duplicated",
                             fcl::exceptions::ctx("key_id", entry.id));
      }
      result.keys.push_back(std::move(entry));
   }
   return result;
}

[[nodiscard]] fcl::crypto::asymmetric::signature sign_digest(const fcl::crypto::asymmetric::private_key& key,
                                                             const fcl::crypto::sha256& digest) {
   using namespace fcl::crypto;
   using storage_type = asymmetric::signature::storage_type;
   switch (key.type()) {
   case asymmetric::algorithm::secp256k1:
      return asymmetric::signature{storage_type{key.as<secp256k1::private_key_shim>().sign(digest, true)}};
   case asymmetric::algorithm::p256:
      return asymmetric::signature{storage_type{key.as<p256::private_key_shim>().sign(digest, true)}};
   case asymmetric::algorithm::ed25519:
      return asymmetric::signature{storage_type{key.as<ed25519::private_key_shim>().sign(digest.to_uint8_span())}};
   case asymmetric::algorithm::rsa:
      return asymmetric::signature{storage_type{key.as<rsa::private_key_shim>().sign(digest.to_uint8_span())}};
   }
   FCL_THROW_EXCEPTION(exceptions::unsupported_algorithm, "unsupported signer key algorithm");
}

[[nodiscard]] bool purpose_allowed(const std::vector<std::string>& allowed, std::string_view value) noexcept {
   return std::ranges::find_if(allowed, [value](const auto& purpose) {
      return purpose == value;
   }) != allowed.end();
}

} // namespace

struct plugin::impl {
   using profile_map = std::map<std::string, fcl::crypto::asymmetric::encoding>;

   struct loaded_key {
      std::string key_id;
      fcl::crypto::asymmetric::private_key private_key;
      std::vector<std::string> purposes;
   };

   explicit impl(plugin_options options) : profiles{make_profiles(std::move(options))} {}

   [[nodiscard]] static profile_map make_profiles(plugin_options options) {
      auto result = profile_map{};
      auto add_profile = [&](const fcl::crypto::asymmetric::text_encoding_profile& profile) {
         auto encoding = fcl::crypto::asymmetric::encoding::from_profile(profile);
         const auto id = encoding.id();
         if (!result.emplace(id, std::move(encoding)).second) {
            FCL_THROW_EXCEPTION(exceptions::invalid_config, "signature provider encoding profile id is duplicated",
                                fcl::exceptions::ctx("profile", id));
         }
      };

      add_profile(fcl::crypto::asymmetric::profiles::fcl());
      add_profile(fcl::crypto::asymmetric::profiles::antelope());
      result.emplace("eos", fcl::crypto::asymmetric::encoding::antelope());
      add_profile(fcl::crypto::asymmetric::profiles::bitcoin());
      add_profile(fcl::crypto::asymmetric::profiles::solana());
      add_profile(fcl::crypto::asymmetric::profiles::tezos());
      for (const auto& profile : options.profiles) {
         add_profile(profile);
      }
      return result;
   }

   [[nodiscard]] const fcl::crypto::asymmetric::encoding& profile_by_name(std::string_view value) const {
      const auto found = profiles.find(std::string{value});
      if (found == profiles.end()) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_profile, "unknown signer encoding profile",
                             fcl::exceptions::ctx("profile", std::string{value}));
      }
      return found->second;
   }

   profile_map profiles;
   std::map<std::string, loaded_key> keys;
   std::string default_output_profile = "fcl";
   bool stopping = false;

   [[nodiscard]] response sign(request value) const {
      const auto found = keys.find(value.key_id);
      if (found == keys.end()) {
         FCL_THROW_EXCEPTION(exceptions::key_not_found, "signer key is not configured",
                             fcl::exceptions::ctx("key_id", value.key_id));
      }

      const auto& key = found->second;
      if (!purpose_allowed(key.purposes, value.purpose)) {
         FCL_THROW_EXCEPTION(exceptions::purpose_denied, "signer key is not allowed for requested purpose",
                             fcl::exceptions::ctx("key_id", value.key_id),
                             fcl::exceptions::ctx("purpose", value.purpose));
      }

      const auto actual_algorithm = to_key_algorithm(key.private_key.type());
      if (value.required_algorithm != key_algorithm::any && value.required_algorithm != actual_algorithm) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_algorithm, "signer key algorithm does not match request");
      }

      const auto profile_name =
         value.output_profile.empty() ? std::string_view{default_output_profile} : std::string_view{value.output_profile};
      const auto& output_profile = profile_by_name(profile_name);
      auto signature = sign_digest(key.private_key, value.digest);
      auto text_signature = std::string{};
      auto public_key = std::string{};
      try {
         text_signature = output_profile.format(signature);
         public_key = output_profile.format(key.private_key.get_public_key());
      } catch (const fcl::crypto::asymmetric::exceptions::invalid_options&) {
         FCL_THROW_EXCEPTION(exceptions::unsupported_profile,
                             "signer output profile does not support this key algorithm",
                             fcl::exceptions::ctx("profile", std::string{profile_name}),
                             fcl::exceptions::ctx("key_id", key.key_id));
      }

      return response{
         .key_id = key.key_id,
         .algorithm = actual_algorithm,
         .output_profile = std::string{profile_name},
         .public_key = std::move(public_key),
         .signature = std::vector<std::uint8_t>(text_signature.begin(), text_signature.end()),
      };
   }
};

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> state) : state_{std::move(state)} {}

   boost::asio::awaitable<response> sign(request value) override {
      co_return state_->sign(std::move(value));
   }

 private:
   std::shared_ptr<impl> state_;
};

plugin::plugin(plugin_options value) : impl_{std::make_shared<impl>(std::move(value))} {}

plugin::~plugin() = default;

fcl::app::plugin_descriptor descriptor(plugin_options value) {
   return fcl::app::plugin_descriptor{
      .id = {.value = "fcl.signature_provider"},
      .factory = [value = std::move(value)] { return std::make_unique<plugin>(value); },
   };
}

fcl::app::plugin_id plugin::id() const {
   return {.value = "fcl.signature_provider"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> plugin::describe_config() const {
   return fcl::config::component_descriptor{
      .section = "signature-provider",
      .fields =
         {
            fcl::config::field_descriptor{
               .name = "keys",
               .kind = fcl::schema::value_kind::object_list,
               .secret = true,
               .description = "Local signer key entries. Private keys are redacted as a whole.",
            },
            fcl::config::field_descriptor{
               .name = "default-output-profile",
               .kind = fcl::schema::value_kind::string,
               .has_default = true,
               .default_value = fcl::config::value{"fcl"},
               .description = "Default signer text encoding profile",
            },
         },
   };
}

boost::asio::awaitable<void> plugin::configure(fcl::config::component_view view) {
   auto config = decode_config(view);
   (void)impl_->profile_by_name(config.default_output_profile);
   auto loaded = std::map<std::string, impl::loaded_key>{};
   for (auto& key : config.keys) {
      const auto& input_profile = impl_->profile_by_name(key.input_profile);
      auto private_key = fcl::crypto::asymmetric::private_key{};
      try {
         private_key = input_profile.parse_private(key.private_key);
      } catch (const std::exception&) {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "signature provider private key is invalid",
                             fcl::exceptions::ctx("key_id", key.id),
                             fcl::exceptions::ctx("input_profile", key.input_profile));
      }
      loaded.emplace(key.id, impl::loaded_key{
                                .key_id = key.id,
                                .private_key = std::move(private_key),
                                .purposes = std::move(key.purposes),
                             });
   }
   impl_->keys = std::move(loaded);
   impl_->default_output_profile = std::move(config.default_output_profile);
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::provide(fcl::api::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(fcl::app::plugin_context&) {
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   co_return;
}

} // namespace fcl::plugins::signature_provider
