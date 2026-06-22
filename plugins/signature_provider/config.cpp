module;

#include <fcl/exceptions/macros.hpp>

#include <exception>
#include <map>
#include <string>
#include <utility>

module fcl.plugins.signature_provider.plugin;

import fcl.config.component;
import fcl.config.decode;
import fcl.crypto.asymmetric;
import fcl.exceptions;
import fcl.plugins.signature_provider.exceptions;
import fcl.plugins.signature_provider.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace fcl::plugins::signature_provider {

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid signature provider config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void apply_config(plugin::impl& state, fcl::config::component_view view) {
   auto config = decode_config(view);
   (void)state.profile_by_name(config.default_output_profile);
   auto loaded = std::map<std::string, plugin::impl::loaded_key>{};
   for (auto& key : config.keys) {
      const auto& input_profile = state.profile_by_name(key.input_profile);
      auto private_key = fcl::crypto::asymmetric::private_key{};
      try {
         private_key = input_profile.parse_private(key.private_key);
      } catch (const std::exception&) {
         FCL_THROW_EXCEPTION(exceptions::invalid_key, "signature provider private key is invalid",
                             fcl::exceptions::ctx("key_id", key.id),
                             fcl::exceptions::ctx("input_profile", key.input_profile));
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

} // namespace fcl::plugins::signature_provider
