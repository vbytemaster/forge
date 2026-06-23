module;

#include <forge/exceptions/macros.hpp>

#include <exception>
#include <map>
#include <string>
#include <utility>

module forge.plugins.crypto.signer.plugin;

import forge.config.component;
import forge.config.decode;
import forge.crypto.asymmetric;
import forge.exceptions;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::crypto::signer {

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                          forge::config::format_decode_diagnostics("invalid crypto signer config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void apply_config(plugin::impl& state, forge::config::component_view view) {
   auto config = decode_config(view);
   (void)state.profile_by_name(config.default_output_profile);
   auto loaded = std::map<std::string, plugin::impl::loaded_key>{};
   for (auto& key : config.keys) {
      const auto& input_profile = state.profile_by_name(key.input_profile);
      auto private_key = forge::crypto::asymmetric::private_key{};
      try {
         private_key = input_profile.parse_private(key.private_key);
      } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "crypto signer private key is invalid",
                             forge::exceptions::ctx("key_id", key.id),
                             forge::exceptions::ctx("input_profile", key.input_profile));
      }
      loaded.emplace(key.id, plugin::impl::loaded_key{
                                .key_id = key.id,
                                .private_key = std::move(private_key),
                                .purposes = std::move(key.purposes),
                             });
   }
   state.keys = std::move(loaded);
   state.default_output_profile = std::move(config.default_output_profile);
   state.stopping = false;
}

} // namespace forge::plugins::crypto::signer
