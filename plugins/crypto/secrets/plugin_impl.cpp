module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.crypto.secrets.plugin;

import forge.crypto.aes;
import forge.crypto.kdf;
import forge.crypto.random;
import forge.crypto.secret_bytes;
import forge.crypto.types;
import forge.exceptions;
import forge.plugins.crypto.secrets.exceptions;
import forge.plugins.crypto.secrets.types;

#include "details/plugin_impl.hxx"

namespace forge::plugins::crypto::secrets {
namespace {

[[nodiscard]] bool contains_purpose(const std::vector<std::string>& allowed, std::string_view purpose) {
   return std::ranges::find(allowed, purpose) != allowed.end();
}

[[nodiscard]] bool contains_operation(const std::vector<operation>& allowed, operation value) {
   return std::ranges::find(allowed, value) != allowed.end();
}

template <typename SecretMap>
[[nodiscard]] const typename SecretMap::mapped_type& find_secret(const SecretMap& secrets, std::string_view id) {
   const auto found = secrets.find(std::string{id});
   if (found == secrets.end()) {
      FORGE_THROW_EXCEPTION(exceptions::secret_not_found, "secret is not configured",
                          forge::exceptions::ctx("secret_id", std::string{id}));
   }
   return found->second;
}

template <typename Secret>
void require_allowed(const Secret& secret, std::string_view purpose, operation value) {
   if (!contains_purpose(secret.purposes, purpose)) {
      FORGE_THROW_EXCEPTION(exceptions::purpose_denied, "secret purpose is not allowed",
                          forge::exceptions::ctx("secret_id", secret.id),
                          forge::exceptions::ctx("purpose", std::string{purpose}));
   }
   if (!contains_operation(secret.operations, value)) {
      FORGE_THROW_EXCEPTION(exceptions::operation_denied, "secret operation is not allowed",
                          forge::exceptions::ctx("secret_id", secret.id));
   }
}

void require_size(std::uint64_t actual, std::uint64_t limit, const char* label) {
   if (actual > limit) {
      FORGE_THROW_EXCEPTION(exceptions::size_limit_exceeded, std::string{label} + " exceeds configured limit");
   }
}

[[nodiscard]] forge::crypto::bytes copy_for_explicit_raw_export(const forge::crypto::secret_bytes& material) {
   return material.copy();
}

template <typename Secret>
[[nodiscard]] forge::crypto::aes256_key aes_key_from_secret(const Secret& secret) {
   try {
      return forge::crypto::make_aes256_key(secret.material.span());
   } catch (const std::exception&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "secret is not a valid AES-256 key",
                          forge::exceptions::ctx("secret_id", secret.id));
   }
}

[[noreturn]] void throw_malformed_aes_gcm_nonce(const std::string& secret_id) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "AES-GCM nonce is malformed",
                       forge::exceptions::ctx("secret_id", secret_id));
}

[[noreturn]] void throw_malformed_aes_gcm_tag(const std::string& secret_id) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "AES-GCM tag is malformed",
                       forge::exceptions::ctx("secret_id", secret_id));
}

[[noreturn]] void throw_malformed_hkdf_request(const std::string& secret_id) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_secret, "HKDF request is malformed",
                       forge::exceptions::ctx("secret_id", secret_id));
}

[[noreturn]] void throw_malformed_aes_gcm_parameter(
   const forge::exceptions::runtime_coded_exception<forge::crypto::aes::exceptions::code>& error,
   const std::string& secret_id) {
   switch (error.value()) {
   case forge::crypto::aes::exceptions::code::invalid_nonce:
      throw_malformed_aes_gcm_nonce(secret_id);
   case forge::crypto::aes::exceptions::code::invalid_tag:
      throw_malformed_aes_gcm_tag(secret_id);
   default:
      throw;
   }
}

[[noreturn]] void throw_malformed_hkdf_parameter(
   const forge::exceptions::runtime_coded_exception<forge::crypto::kdf::exceptions::code>& error,
   const std::string& secret_id) {
   switch (error.value()) {
   case forge::crypto::kdf::exceptions::code::invalid_options:
      throw_malformed_hkdf_request(secret_id);
   default:
      throw;
   }
}

} // namespace

snapshot plugin::impl::status(query) const {
   auto summaries = std::vector<secret_summary>{};
   summaries.reserve(secrets.size());
   for (const auto& [ignored, secret] : secrets) {
      summaries.push_back(secret_summary{
         .id = secret.id,
         .kind = secret.kind,
         .purposes = secret.purposes,
         .operations = secret.operations,
         .allow_raw_export = secret.allow_raw_export,
      });
   }
   return snapshot{
      .configured_secrets = static_cast<std::uint64_t>(secrets.size()),
      .stopping = stopping,
      .secrets = std::move(summaries),
   };
}

get_result plugin::impl::get_bytes(get_request value) const {
   const auto& secret = find_secret(secrets, value.secret_id);
   require_allowed(secret, value.purpose, operation::get_bytes);
   if (!secret.allow_raw_export) {
      FORGE_THROW_EXCEPTION(exceptions::operation_denied, "raw secret export is disabled",
                          forge::exceptions::ctx("secret_id", secret.id));
   }
   return get_result{
      .secret_id = secret.id,
      .bytes = copy_for_explicit_raw_export(secret.material),
   };
}

derive_result plugin::impl::derive_hkdf_sha256(derive_request value) const {
   const auto& secret = find_secret(secrets, value.secret_id);
   require_allowed(secret, value.purpose, operation::derive_hkdf_sha256);
   try {
      auto output = forge::crypto::derive_hkdf_sha256(forge::crypto::hkdf_sha256_span_request{
         .secret = secret.material.span(),
         .salt = value.salt,
         .info = value.info,
         .output_size = static_cast<std::size_t>(value.output_size),
      });
      return derive_result{.secret_id = secret.id, .bytes = std::move(output)};
   } catch (const forge::crypto::kdf::exceptions::invalid_options&) {
      throw_malformed_hkdf_request(secret.id);
   } catch (const forge::exceptions::runtime_coded_exception<forge::crypto::kdf::exceptions::code>& error) {
      throw_malformed_hkdf_parameter(error, secret.id);
   }
}

aead_encrypt_result plugin::impl::encrypt_aes_gcm(aead_encrypt_request value) const {
   const auto& secret = find_secret(secrets, value.secret_id);
   require_allowed(secret, value.purpose, operation::encrypt_aes_gcm);
   require_size(value.plaintext.size(), secret.max_plaintext_bytes, "plaintext");
   require_size(value.aad.size(), secret.max_aad_bytes, "AAD");

   auto nonce = std::move(value.nonce);
   if (nonce.empty()) {
      nonce = forge::crypto::random_bytes(forge::crypto::aes_gcm_nonce_size);
   }
   try {
      auto encrypted = forge::crypto::encrypt_aes256_gcm(forge::crypto::aes256_gcm_encrypt_request{
         .key = aes_key_from_secret(secret),
         .nonce = std::move(nonce),
         .plaintext = std::move(value.plaintext),
         .aad = std::move(value.aad),
      });
      return aead_encrypt_result{
         .secret_id = secret.id,
         .nonce = std::move(encrypted.nonce),
         .tag = std::move(encrypted.tag),
         .ciphertext = std::move(encrypted.ciphertext),
      };
   } catch (const forge::crypto::aes::exceptions::invalid_nonce&) {
      throw_malformed_aes_gcm_nonce(secret.id);
   } catch (const forge::exceptions::runtime_coded_exception<forge::crypto::aes::exceptions::code>& error) {
      throw_malformed_aes_gcm_parameter(error, secret.id);
   }
}

aead_decrypt_result plugin::impl::decrypt_aes_gcm(aead_decrypt_request value) const {
   const auto& secret = find_secret(secrets, value.secret_id);
   require_allowed(secret, value.purpose, operation::decrypt_aes_gcm);
   require_size(value.ciphertext.size(), secret.max_ciphertext_bytes, "ciphertext");
   require_size(value.ciphertext.size(), secret.max_plaintext_bytes, "plaintext");
   require_size(value.aad.size(), secret.max_aad_bytes, "AAD");

   try {
      auto plaintext = forge::crypto::decrypt_aes256_gcm(forge::crypto::aes256_gcm_decrypt_request{
         .key = aes_key_from_secret(secret),
         .encrypted =
            forge::crypto::aes256_gcm_ciphertext{
               .nonce = std::move(value.nonce),
               .tag = std::move(value.tag),
               .ciphertext = std::move(value.ciphertext),
            },
         .aad = std::move(value.aad),
      });
      require_size(plaintext.size(), secret.max_plaintext_bytes, "plaintext");
      return aead_decrypt_result{.secret_id = secret.id, .plaintext = std::move(plaintext)};
   } catch (const forge::crypto::aes::exceptions::invalid_nonce&) {
      throw_malformed_aes_gcm_nonce(secret.id);
   } catch (const forge::crypto::aes::exceptions::invalid_tag&) {
      throw_malformed_aes_gcm_tag(secret.id);
   } catch (const forge::exceptions::runtime_coded_exception<forge::crypto::aes::exceptions::code>& error) {
      throw_malformed_aes_gcm_parameter(error, secret.id);
   } catch (const forge::crypto::aes::exceptions::authentication_failed&) {
      FORGE_THROW_EXCEPTION(exceptions::crypto_failed, "AES-GCM authentication failed",
                          forge::exceptions::ctx("secret_id", secret.id));
   }
}

} // namespace forge::plugins::crypto::secrets
