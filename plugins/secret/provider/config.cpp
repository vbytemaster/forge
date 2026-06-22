module;

#include <fcl/exceptions/macros.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.secret.provider.plugin;

import fcl.config.component;
import fcl.config.decode;
import fcl.crypto.secret_bytes;
import fcl.exceptions;
import fcl.plugins.secret.provider.exceptions;
import fcl.plugins.secret.provider.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/source_loading.hxx"

namespace fcl::plugins::secret::provider {
namespace {

[[nodiscard]] std::uint64_t resolved_limit(std::uint64_t value, std::uint64_t fallback) {
   return value == 0 ? fallback : value;
}

void require_aes_update_limit(std::uint64_t value, const char* label) {
   if (value > aes_update_bytes_ceiling) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, std::string{label} + " exceeds AES update ceiling");
   }
}

} // namespace

config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FCL_THROW_EXCEPTION(exceptions::invalid_config,
                          fcl::config::format_decode_diagnostics("invalid secret provider config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void apply_config(plugin::impl& state, fcl::config::component_view view) {
   auto decoded = decode_config(view);
   require_aes_update_limit(decoded.default_max_plaintext_bytes, "default-max-plaintext-bytes");
   require_aes_update_limit(decoded.default_max_ciphertext_bytes, "default-max-ciphertext-bytes");
   require_aes_update_limit(decoded.default_max_aad_bytes, "default-max-aad-bytes");

   auto loaded = std::map<std::string, plugin::impl::loaded_secret>{};
   const auto decrypt_limits = encrypted_file_decrypt_limits{
      .max_plaintext_bytes = decoded.default_max_plaintext_bytes,
      .max_scrypt_n = decoded.encrypted_file_max_scrypt_n,
      .max_scrypt_r = decoded.encrypted_file_max_scrypt_r,
      .max_scrypt_p = decoded.encrypted_file_max_scrypt_p,
      .max_scrypt_memory_bytes = decoded.encrypted_file_max_scrypt_memory_bytes,
   };
   for (auto& entry : decoded.secrets) {
      auto max_plaintext = resolved_limit(entry.max_plaintext_bytes, decoded.default_max_plaintext_bytes);
      auto max_ciphertext = resolved_limit(entry.max_ciphertext_bytes, decoded.default_max_ciphertext_bytes);
      auto max_aad = resolved_limit(entry.max_aad_bytes, decoded.default_max_aad_bytes);
      require_aes_update_limit(max_plaintext, "max-plaintext-bytes");
      require_aes_update_limit(max_ciphertext, "max-ciphertext-bytes");
      require_aes_update_limit(max_aad, "max-aad-bytes");
      auto material = load_secret_material(entry, max_plaintext, max_ciphertext, decrypt_limits);
      loaded.emplace(entry.id,
                     plugin::impl::loaded_secret{
                        .id = entry.id,
                        .kind = entry.kind,
                        .material = std::move(material),
                        .purposes = std::move(entry.purposes),
                        .operations = std::move(entry.operations),
                        .allow_raw_export = entry.allow_raw_export,
                        .max_plaintext_bytes = max_plaintext,
                        .max_ciphertext_bytes = max_ciphertext,
                        .max_aad_bytes = max_aad,
                     });
   }
   state.secrets = std::move(loaded);
   state.stopping = false;
}

} // namespace fcl::plugins::secret::provider
