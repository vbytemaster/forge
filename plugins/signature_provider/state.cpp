module;

#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module fcl.plugins.signature_provider.plugin;

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

#include "details/state.hxx"

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

plugin::impl::impl(plugin_options options) : profiles{make_profiles(std::move(options))} {}

plugin::impl::profile_map plugin::impl::make_profiles(plugin_options options) {
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

const fcl::crypto::asymmetric::encoding& plugin::impl::profile_by_name(std::string_view value) const {
   const auto found = profiles.find(std::string{value});
   if (found == profiles.end()) {
      FCL_THROW_EXCEPTION(exceptions::unsupported_profile, "unknown signer encoding profile",
                          fcl::exceptions::ctx("profile", std::string{value}));
   }
   return found->second;
}

response plugin::impl::sign(request value) const {
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

} // namespace fcl::plugins::signature_provider
