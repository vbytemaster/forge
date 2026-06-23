#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import forge.api.registry;
import forge.app.plugin;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.config.component;
import forge.config.decode;
import forge.config.document;
import forge.config.value;
import forge.crypto.aes;
import forge.crypto.base64;
import forge.crypto.kdf;
import forge.crypto.secret_bytes;
import forge.crypto.types;
import forge.env;
import forge.plugins.crypto.secrets.api;
import forge.plugins.crypto.secrets.exceptions;
import forge.plugins.crypto.secrets.plugin;
import forge.plugins.crypto.secrets.types;
import forge.program_options;
import forge.schema.value_kind;

#include "details/source_loading.hxx"

namespace crypto_secrets = forge::plugins::crypto::secrets;

namespace {

constexpr auto crypto_secrets_section = "plugins.crypto.secrets";

[[nodiscard]] forge::config::value array(std::vector<std::string> values) {
   auto result = forge::config::value::array_type{};
   for (auto& value : values) {
      result.emplace_back(std::move(value));
   }
   return forge::config::value{std::move(result)};
}

[[nodiscard]] forge::config::value source_value(std::string value,
                                              crypto_secrets::encoding encoding = crypto_secrets::encoding::raw) {
   auto object = forge::config::value::object_type{};
   object.emplace("type", forge::config::value{"value"});
   object.emplace("encoding", forge::config::value{encoding == crypto_secrets::encoding::hex ? "hex" :
                                                 encoding == crypto_secrets::encoding::base64 ? "base64" : "raw"});
   object.emplace("value", forge::config::value{std::move(value)});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value source_file(const std::filesystem::path& path,
                                             crypto_secrets::encoding encoding = crypto_secrets::encoding::raw) {
   auto object = forge::config::value::object_type{};
   object.emplace("type", forge::config::value{"file"});
   object.emplace("encoding", forge::config::value{encoding == crypto_secrets::encoding::hex ? "hex" :
                                                 encoding == crypto_secrets::encoding::base64 ? "base64" : "raw"});
   object.emplace("path", forge::config::value{path.string()});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value source_encrypted_file(const std::filesystem::path& path, std::string passphrase) {
   auto object = forge::config::value::object_type{};
   object.emplace("type", forge::config::value{"encrypted_file"});
   object.emplace("path", forge::config::value{path.string()});
   object.emplace("passphrase-value", forge::config::value{std::move(passphrase)});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value source_encrypted_file_with_passphrase_file(const std::filesystem::path& path,
                                                                            const std::filesystem::path& passphrase) {
   auto object = forge::config::value::object_type{};
   object.emplace("type", forge::config::value{"encrypted_file"});
   object.emplace("path", forge::config::value{path.string()});
   object.emplace("passphrase-file", forge::config::value{passphrase.string()});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value secret_entry(std::string id,
                                              forge::config::value source,
                                              std::vector<std::string> purposes,
                                              std::vector<std::string> operations,
                                              bool allow_raw_export = false) {
   auto object = forge::config::value::object_type{};
   object.emplace("id", forge::config::value{std::move(id)});
   object.emplace("kind", forge::config::value{"symmetric_key"});
   object.emplace("source", std::move(source));
   object.emplace("purposes", array(std::move(purposes)));
   object.emplace("operations", array(std::move(operations)));
   object.emplace("allow-raw-export", forge::config::value{allow_raw_export});
   return forge::config::value{std::move(object)};
}

[[nodiscard]] forge::config::value secret_entry_with_limit(std::string limit_name, std::uint64_t limit) {
   auto entry = secret_entry("data-key", source_value(std::string(32, 'K')), {"payload.encrypt"}, {"encrypt_aes_gcm"});
   auto& object = std::get<forge::config::value::object_type>(entry.storage);
   object.emplace(std::move(limit_name), forge::config::value{limit});
   return entry;
}

[[nodiscard]] forge::config::document secrets_config(std::vector<forge::config::value> secrets) {
   auto document = forge::config::document{};
   document.set("plugins.crypto.secrets.secrets", forge::config::value::array_type(secrets.begin(), secrets.end()));
   return document;
}

void write_secret_file(const std::filesystem::path& path, const forge::crypto::bytes& value) {
   auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
   out.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
}

void overwrite_u64_le(forge::crypto::bytes& value, std::size_t offset, std::uint64_t replacement) {
   for (auto i = 0U; i < 8U; ++i) {
      value[offset + i] = static_cast<std::uint8_t>((replacement >> (i * 8U)) & 0xffU);
   }
}

[[nodiscard]] forge::api::handle<crypto_secrets::api> configured_api(forge::asio::runtime& runtime,
                                                                    crypto_secrets::plugin& plugin,
                                                                    const forge::config::document& document) {
   forge::asio::blocking::run(runtime, plugin.configure(forge::config::component_view{document, crypto_secrets_section}));
   auto registry = forge::api::registry{};
   auto installer = forge::api::installer{registry};
   forge::asio::blocking::run(runtime, plugin.provide(installer));
   return registry.get<crypto_secrets::api>(crypto_secrets::api::ref());
}

[[nodiscard]] forge::crypto::bytes bytes(std::string_view value) {
   return forge::crypto::bytes{value.begin(), value.end()};
}

[[nodiscard]] crypto_secrets::encrypted_file_decrypt_limits default_decrypt_limits(
   std::uint64_t max_plaintext_bytes = crypto_secrets::default_max_plaintext_bytes) {
   return crypto_secrets::encrypted_file_decrypt_limits{
      .max_plaintext_bytes = max_plaintext_bytes,
      .max_scrypt_n = crypto_secrets::default_encrypted_file_max_scrypt_n,
      .max_scrypt_r = crypto_secrets::default_encrypted_file_max_scrypt_r,
      .max_scrypt_p = crypto_secrets::default_encrypted_file_max_scrypt_p,
      .max_scrypt_memory_bytes = crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes,
   };
}

[[nodiscard]] std::string write_temp_file(std::string name, std::string_view value) {
   const auto path = std::filesystem::temp_directory_path() / name;
   auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
   out.write(value.data(), static_cast<std::streamsize>(value.size()));
   return path.string();
}

} // namespace

BOOST_AUTO_TEST_SUITE(crypto_secrets_tests)

BOOST_AUTO_TEST_CASE(crypto_secrets_config_direct_defaults_match_schema_constants) try {
   const auto value = crypto_secrets::config{};

   BOOST_TEST(value.default_max_plaintext_bytes == crypto_secrets::default_max_plaintext_bytes);
   BOOST_TEST(value.default_max_ciphertext_bytes == crypto_secrets::default_max_ciphertext_bytes);
   BOOST_TEST(value.default_max_aad_bytes == crypto_secrets::default_max_aad_bytes);
   BOOST_TEST(value.default_max_plaintext_bytes <= crypto_secrets::aes_update_bytes_ceiling);
   BOOST_TEST(value.default_max_ciphertext_bytes <= crypto_secrets::aes_update_bytes_ceiling);
   BOOST_TEST(value.default_max_aad_bytes <= crypto_secrets::aes_update_bytes_ceiling);
   BOOST_TEST(value.encrypted_file_max_scrypt_n == crypto_secrets::default_encrypted_file_max_scrypt_n);
   BOOST_TEST(value.encrypted_file_max_scrypt_r == crypto_secrets::default_encrypted_file_max_scrypt_r);
   BOOST_TEST(value.encrypted_file_max_scrypt_p == crypto_secrets::default_encrypted_file_max_scrypt_p);
   BOOST_TEST(value.encrypted_file_max_scrypt_memory_bytes ==
              crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_descriptor_redacts_config_and_keeps_api_local) try {
   static_assert(forge::api::local_interface<crypto_secrets::api>);
   static_assert(!forge::api::remote_interface<crypto_secrets::api>);

   auto plugin = crypto_secrets::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == crypto_secrets_section);

   const auto secrets = std::ranges::find_if(descriptor->fields, [](const auto& field) {
      return field.name == "secrets";
   });
   BOOST_REQUIRE(secrets != descriptor->fields.end());
   BOOST_TEST(secrets->secret);
   BOOST_TEST(static_cast<int>(secrets->kind) == static_cast<int>(forge::schema::value_kind::object_list));

   const auto has_default_field = [&](std::string_view name, std::uint64_t expected) {
      const auto found = std::ranges::find_if(descriptor->fields, [&](const auto& field) {
         return field.name == name;
      });
      BOOST_REQUIRE(found != descriptor->fields.end());
      BOOST_TEST(found->has_default);
      BOOST_TEST(std::get<std::uint64_t>(found->default_value.storage) == expected);
   };
   has_default_field("default-max-plaintext-bytes", crypto_secrets::default_max_plaintext_bytes);
   has_default_field("default-max-ciphertext-bytes", crypto_secrets::default_max_ciphertext_bytes);
   has_default_field("default-max-aad-bytes", crypto_secrets::default_max_aad_bytes);
   has_default_field("encrypted-file-max-scrypt-n", crypto_secrets::default_encrypted_file_max_scrypt_n);
   has_default_field("encrypted-file-max-scrypt-r", crypto_secrets::default_encrypted_file_max_scrypt_r);
   has_default_field("encrypted-file-max-scrypt-p", crypto_secrets::default_encrypted_file_max_scrypt_p);
   has_default_field("encrypted-file-max-scrypt-memory-bytes",
                     crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes);

   auto registry = forge::config::component_registry{};
   registry.add(*descriptor);
   const auto redacted = forge::config::redact(
      secrets_config({secret_entry("session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"}, true)}),
      registry);
   const auto* value = redacted.try_get("plugins.crypto.secrets.secrets");
   BOOST_REQUIRE(value != nullptr);
   const auto* text = std::get_if<std::string>(&value->storage);
   BOOST_REQUIRE(text != nullptr);
   BOOST_TEST(*text == "<redacted>");
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_rejects_default_payload_limits_above_aes_update_ceiling) try {
   const auto too_large = crypto_secrets::aes_update_bytes_ceiling + 1U;

   auto plaintext_document = secrets_config(
      {secret_entry("data-key", source_value(std::string(32, 'K')), {"payload.encrypt"}, {"encrypt_aes_gcm"})});
   plaintext_document.set("plugins.crypto.secrets.default-max-plaintext-bytes", forge::config::value{too_large});
   auto plaintext_plugin = crypto_secrets::plugin{};
   auto plaintext_runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        plaintext_runtime,
                        plaintext_plugin.configure(
                           forge::config::component_view{plaintext_document, crypto_secrets_section})),
                     crypto_secrets::exceptions::invalid_config);

   auto ciphertext_document = secrets_config(
      {secret_entry("data-key", source_value(std::string(32, 'K')), {"payload.decrypt"}, {"decrypt_aes_gcm"})});
   ciphertext_document.set("plugins.crypto.secrets.default-max-ciphertext-bytes", forge::config::value{too_large});
   auto ciphertext_plugin = crypto_secrets::plugin{};
   auto ciphertext_runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(forge::asio::blocking::run(ciphertext_runtime,
                                              ciphertext_plugin.configure(
                                                 forge::config::component_view{ciphertext_document,
                                                                             crypto_secrets_section})),
                     crypto_secrets::exceptions::invalid_config);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_rejects_per_secret_payload_limits_above_aes_update_ceiling) try {
   const auto too_large = crypto_secrets::aes_update_bytes_ceiling + 1U;

   auto plaintext_plugin = crypto_secrets::plugin{};
   auto plaintext_runtime = forge::asio::runtime{};
   const auto plaintext_document = secrets_config({secret_entry_with_limit("max-plaintext-bytes", too_large)});
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        plaintext_runtime,
                        plaintext_plugin.configure(
                           forge::config::component_view{plaintext_document, crypto_secrets_section})),
                     crypto_secrets::exceptions::invalid_config);

   auto ciphertext_plugin = crypto_secrets::plugin{};
   auto ciphertext_runtime = forge::asio::runtime{};
   const auto ciphertext_document = secrets_config({secret_entry_with_limit("max-ciphertext-bytes", too_large)});
   BOOST_CHECK_THROW(forge::asio::blocking::run(ciphertext_runtime,
                                              ciphertext_plugin.configure(
                                                 forge::config::component_view{ciphertext_document,
                                                                             crypto_secrets_section})),
                     crypto_secrets::exceptions::invalid_config);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_value_source_denies_raw_export_by_default) try {
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry(
                                "session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"})}));

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->get_bytes(crypto_secrets::get_request{
                                                 .secret_id = "session",
                                                 .purpose = "payload.decrypt",
                                              })),
                     crypto_secrets::exceptions::operation_denied);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_value_source_exports_only_when_allowed) try {
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry(
                                "session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"}, true)}));

   const auto result = forge::asio::blocking::run(runtime,
                                               api->get_bytes(crypto_secrets::get_request{
                                                  .secret_id = "session",
                                                  .purpose = "payload.decrypt",
                                               }));

   BOOST_TEST(result.secret_id == "session");
   BOOST_TEST(result.bytes == bytes("super-secret"));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_file_source_loads_bounded_secret_without_env_parser) try {
   const auto path = write_temp_file("forge-crypto-secrets-file-source.txt", "file-secret");
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("file",
                                                           source_file(path),
                                                           {"payload.decrypt"},
                                                           {"get_bytes"},
                                                           true)}));

   const auto result = forge::asio::blocking::run(runtime,
                                               api->get_bytes(crypto_secrets::get_request{
                                                  .secret_id = "file",
                                                  .purpose = "payload.decrypt",
                                               }));

   BOOST_TEST(result.bytes == bytes("file-secret"));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_file_source_rejects_short_read) try {
   auto input = std::istringstream{"abc"};
   auto output = forge::crypto::bytes(5U);
   input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));

   BOOST_CHECK_THROW(crypto_secrets::require_complete_file_read(input, output.size(), "/tmp/short-secret", "short"),
                     crypto_secrets::exceptions::invalid_source);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_hkdf_and_aes_gcm_are_purpose_gated) try {
   const auto key = std::string(32, 'K');
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry(
                                "data-key",
                                source_value(key),
                                {"payload.decrypt"},
                                {"derive_hkdf_sha256", "decrypt_aes_gcm"})}));

   const auto derived = forge::asio::blocking::run(runtime,
                                                api->derive_hkdf_sha256(crypto_secrets::derive_request{
                                                   .secret_id = "data-key",
                                                   .purpose = "payload.decrypt",
                                                   .salt = bytes("salt"),
                                                   .info = bytes("info"),
                                                   .output_size = 32,
                                                }));
   BOOST_TEST(derived.bytes.size() == 32U);

   auto aes_key = forge::crypto::make_aes256_key(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(key.data()), key.size()});
   const auto encrypted = forge::crypto::encrypt_aes256_gcm({
      .key = aes_key,
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .plaintext = bytes("payload"),
      .aad = bytes("aad"),
   });

   const auto decrypted = forge::asio::blocking::run(runtime,
                                                  api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                     .secret_id = "data-key",
                                                     .purpose = "payload.decrypt",
                                                     .nonce = encrypted.nonce,
                                                     .tag = encrypted.tag,
                                                     .ciphertext = encrypted.ciphertext,
                                                     .aad = bytes("aad"),
                                                  }));
   BOOST_TEST(decrypted.plaintext == bytes("payload"));

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->derive_hkdf_sha256(crypto_secrets::derive_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "other",
                                                 .salt = bytes("salt"),
                                                 .info = bytes("info"),
                                                 .output_size = 32,
                                              })),
                     crypto_secrets::exceptions::purpose_denied);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_hkdf_invalid_output_size_maps_to_invalid_secret) try {
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry(
                                "data-key",
                                source_value("derive-secret"),
                                {"data-key.derivation"},
                                {"derive_hkdf_sha256"})}));

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->derive_hkdf_sha256(crypto_secrets::derive_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "data-key.derivation",
                                                 .salt = bytes("salt"),
                                                 .info = bytes("info"),
                                                 .output_size = 0,
                                              })),
                     crypto_secrets::exceptions::invalid_secret);

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->derive_hkdf_sha256(crypto_secrets::derive_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "data-key.derivation",
                                                 .salt = bytes("salt"),
                                                 .info = bytes("info"),
                                                 .output_size = 255U * 32U + 1U,
                                              })),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypt_aes_gcm_maps_malformed_nonce) try {
   const auto key = std::string(32, 'K');
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("data-key",
                                                           source_value(key),
                                                           {"payload.encrypt"},
                                                           {"encrypt_aes_gcm"})}));

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->encrypt_aes_gcm(crypto_secrets::aead_encrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.encrypt",
                                                 .nonce = {0, 1, 2},
                                                 .plaintext = bytes("payload"),
                                                 .aad = bytes("aad"),
                                              })),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypt_aes_gcm_rejects_oversized_aad_before_crypto) try {
   const auto key = std::string(32, 'K');
   auto document = secrets_config({secret_entry("data-key",
                                                source_value(key),
                                                {"payload.encrypt"},
                                                {"encrypt_aes_gcm"})});
   document.set("plugins.crypto.secrets.default-max-aad-bytes", forge::config::value{4U});

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime, plugin, document);

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->encrypt_aes_gcm(crypto_secrets::aead_encrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.encrypt",
                                                 .plaintext = bytes("payload"),
                                                 .aad = bytes("oversized-aad"),
                                              })),
                     crypto_secrets::exceptions::size_limit_exceeded);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_aes_gcm_maps_malformed_parameters) try {
   const auto key = std::string(32, 'K');
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("data-key",
                                                           source_value(key),
                                                           {"payload.decrypt"},
                                                           {"decrypt_aes_gcm"})}));

   auto aes_key = forge::crypto::make_aes256_key(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(key.data()), key.size()});
   const auto encrypted = forge::crypto::encrypt_aes256_gcm({
      .key = aes_key,
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .plaintext = bytes("payload"),
      .aad = bytes("aad"),
   });

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.decrypt",
                                                 .nonce = {0, 1, 2},
                                                 .tag = encrypted.tag,
                                                 .ciphertext = encrypted.ciphertext,
                                                 .aad = bytes("aad"),
                                              })),
                     crypto_secrets::exceptions::invalid_secret);

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.decrypt",
                                                 .nonce = encrypted.nonce,
                                                 .tag = {0, 1, 2},
                                                 .ciphertext = encrypted.ciphertext,
                                                 .aad = bytes("aad"),
                                              })),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_aes_gcm_rejects_oversized_aad_before_crypto) try {
   const auto key = std::string(32, 'K');
   auto document = secrets_config({secret_entry("data-key",
                                                source_value(key),
                                                {"payload.decrypt"},
                                                {"decrypt_aes_gcm"})});
   document.set("plugins.crypto.secrets.default-max-aad-bytes", forge::config::value{4U});

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime, plugin, document);

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.decrypt",
                                                 .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
                                                 .tag = std::vector<std::uint8_t>(forge::crypto::aes_gcm_tag_size, 0),
                                                 .ciphertext = bytes("ciphertext"),
                                                 .aad = bytes("oversized-aad"),
                                              })),
                     crypto_secrets::exceptions::size_limit_exceeded);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_aes_gcm_rejects_oversized_plaintext_before_crypto) try {
   const auto key = std::string(32, 'K');
   auto document = secrets_config({secret_entry("data-key",
                                                source_value(key),
                                                {"payload.decrypt"},
                                                {"decrypt_aes_gcm"})});
   document.set("plugins.crypto.secrets.default-max-plaintext-bytes", forge::config::value{32U});

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime, plugin, document);

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.decrypt",
                                                 .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
                                                 .tag = std::vector<std::uint8_t>(forge::crypto::aes_gcm_tag_size, 0),
                                                 .ciphertext = forge::crypto::bytes(33U, std::uint8_t{0}),
                                              })),
                     crypto_secrets::exceptions::size_limit_exceeded);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_aes_gcm_keeps_authentication_failure_typed) try {
   const auto key = std::string(32, 'K');
   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("data-key",
                                                           source_value(key),
                                                           {"payload.decrypt"},
                                                           {"decrypt_aes_gcm"})}));

   auto aes_key = forge::crypto::make_aes256_key(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(key.data()), key.size()});
   auto encrypted = forge::crypto::encrypt_aes256_gcm({
      .key = aes_key,
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .plaintext = bytes("payload"),
      .aad = bytes("aad"),
   });
   encrypted.tag.front() ^= 0xffU;

   BOOST_CHECK_THROW(forge::asio::blocking::run(runtime,
                                              api->decrypt_aes_gcm(crypto_secrets::aead_decrypt_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "payload.decrypt",
                                                 .nonce = encrypted.nonce,
                                                 .tag = encrypted.tag,
                                                 .ciphertext = encrypted.ciphertext,
                                                 .aad = bytes("aad"),
                                              })),
                     crypto_secrets::exceptions::crypto_failed);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypted_file_roundtrips_and_rejects_wrong_passphrase) try {
   const auto path = std::filesystem::temp_directory_path() / "forge-crypto-secrets-encrypted-source.bin";
   const auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("encrypted",
                                                           source_encrypted_file(path, "correct horse battery staple"),
                                                           {"payload.decrypt"},
                                                           {"get_bytes"},
                                                           true)}));
   const auto result = forge::asio::blocking::run(runtime,
                                               api->get_bytes(crypto_secrets::get_request{
                                                  .secret_id = "encrypted",
                                                  .purpose = "payload.decrypt",
                                               }));
   BOOST_TEST(result.bytes == bytes("encrypted-secret"));

   auto wrong = crypto_secrets::plugin{};
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         wrong.configure(forge::config::component_view{
            secrets_config({secret_entry("encrypted",
                                          source_encrypted_file(path, "wrong passphrase"),
                                          {"payload.decrypt"},
                                          {"get_bytes"},
                                          true)}),
            crypto_secrets_section})),
      crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_secret_file_maps_malformed_container_to_invalid_secret) try {
   auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });

   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(bytes("bad"), "passphrase", default_decrypt_limits()),
                     crypto_secrets::exceptions::invalid_secret);

   auto truncated_header = container;
   truncated_header.resize(9U);
   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        truncated_header, "correct horse battery staple", default_decrypt_limits()),
                     crypto_secrets::exceptions::invalid_secret);

   auto truncated_body = container;
   truncated_body.pop_back();
   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        truncated_body, "correct horse battery staple", default_decrypt_limits()),
                     crypto_secrets::exceptions::invalid_secret);

   auto trailing_bytes = container;
   trailing_bytes.push_back(0xffU);
   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        trailing_bytes, "correct horse battery staple", default_decrypt_limits()),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_secret_file_maps_kdf_failures_to_invalid_secret) try {
   auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   overwrite_u64_le(container, 8U, 3U);

   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        container, "correct horse battery staple", default_decrypt_limits()),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_decrypt_secret_file_maps_crypto_failures_to_invalid_secret) try {
   auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });

   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        container,
                        "wrong passphrase",
                        crypto_secrets::encrypted_file_decrypt_limits{
                           .max_plaintext_bytes = crypto_secrets::default_max_plaintext_bytes,
                           .max_scrypt_n = crypto_secrets::default_encrypted_file_max_scrypt_n,
                           .max_scrypt_r = crypto_secrets::default_encrypted_file_max_scrypt_r,
                           .max_scrypt_p = crypto_secrets::default_encrypted_file_max_scrypt_p,
                           .max_scrypt_memory_bytes = crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes,
                        }),
                     crypto_secrets::exceptions::invalid_secret);

   const auto tag_offset = 8U + (7U * 8U) + bytes("0123456789abcdef").size() + 12U;
   container[tag_offset] ^= 0xffU;

   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        container,
                        "correct horse battery staple",
                        crypto_secrets::encrypted_file_decrypt_limits{
                           .max_plaintext_bytes = crypto_secrets::default_max_plaintext_bytes,
                           .max_scrypt_n = crypto_secrets::default_encrypted_file_max_scrypt_n,
                           .max_scrypt_r = crypto_secrets::default_encrypted_file_max_scrypt_r,
                           .max_scrypt_p = crypto_secrets::default_encrypted_file_max_scrypt_p,
                           .max_scrypt_memory_bytes = crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes,
                        }),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypted_file_plaintext_limit_is_size_limit_exceeded) try {
   const auto path = std::filesystem::temp_directory_path() / "forge-crypto-secrets-encrypted-source-too-large.bin";
   const auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto document = secrets_config({secret_entry("encrypted",
                                                source_encrypted_file(path, "correct horse battery staple"),
                                                {"payload.decrypt"},
                                                {"get_bytes"},
                                                true)});
   document.set("plugins.crypto.secrets.default-max-plaintext-bytes", forge::config::value{4U});

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        runtime, plugin.configure(forge::config::component_view{document, crypto_secrets_section})),
                     crypto_secrets::exceptions::size_limit_exceeded);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypted_file_default_ciphertext_limit_allows_max_plaintext) try {
   const auto path = std::filesystem::temp_directory_path() / "forge-crypto-secrets-encrypted-source-default-limit.bin";
   const auto passphrase = std::string{"correct horse battery staple"};
   const auto plaintext = forge::crypto::bytes(crypto_secrets::default_max_plaintext_bytes, std::uint8_t{'x'});
   const auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = plaintext,
      .passphrase = passphrase,
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   BOOST_TEST(container.size() > crypto_secrets::default_max_plaintext_bytes);
   BOOST_TEST(container.size() <= crypto_secrets::default_max_ciphertext_bytes);
   write_secret_file(path, container);

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             secrets_config({secret_entry("encrypted",
                                                           source_encrypted_file(path, passphrase),
                                                           {"payload.decrypt"},
                                                           {"get_bytes"},
                                                           true)}));

   const auto exported = forge::asio::blocking::run(runtime,
                                                 api->get_bytes(crypto_secrets::get_request{
                                                    .secret_id = "encrypted",
                                                    .purpose = "payload.decrypt",
                                                 }));
   BOOST_TEST(exported.bytes == plaintext);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypted_file_missing_passphrase_file_is_invalid_source) try {
   const auto path = std::filesystem::temp_directory_path() / "forge-crypto-secrets-encrypted-missing-passphrase.bin";
   const auto missing_passphrase =
      std::filesystem::temp_directory_path() / "forge-crypto-secrets-missing-passphrase-file.txt";
   std::filesystem::remove(missing_passphrase);
   const auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(
      forge::asio::blocking::run(
         runtime,
         plugin.configure(forge::config::component_view{
            secrets_config({secret_entry("encrypted",
                                          source_encrypted_file_with_passphrase_file(path, missing_passphrase),
                                          {"payload.decrypt"},
                                          {"get_bytes"},
                                          true)}),
            crypto_secrets_section})),
      crypto_secrets::exceptions::invalid_source);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_encrypted_file_rejects_scrypt_params_above_limits_before_kdf) try {
   auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   overwrite_u64_le(container, 8U, 2048U);

   BOOST_CHECK_THROW((void)crypto_secrets::decrypt_secret_file(
                        container,
                        "correct horse battery staple",
                        crypto_secrets::encrypted_file_decrypt_limits{
                           .max_plaintext_bytes = crypto_secrets::default_max_plaintext_bytes,
                           .max_scrypt_n = 1024,
                           .max_scrypt_r = crypto_secrets::default_encrypted_file_max_scrypt_r,
                           .max_scrypt_p = crypto_secrets::default_encrypted_file_max_scrypt_p,
                           .max_scrypt_memory_bytes = crypto_secrets::default_encrypted_file_max_scrypt_memory_bytes,
                        }),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_config_rejects_encrypted_file_scrypt_params_above_configured_ceilings) try {
   const auto path = std::filesystem::temp_directory_path() / "forge-crypto-secrets-encrypted-source-high-scrypt.bin";
   const auto container = crypto_secrets::encrypt_secret_file(crypto_secrets::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto document = secrets_config({secret_entry("encrypted",
                                                source_encrypted_file(path, "correct horse battery staple"),
                                                {"payload.decrypt"},
                                                {"get_bytes"},
                                                true)});
   document.set("plugins.crypto.secrets.encrypted-file-max-scrypt-n", forge::config::value{512U});

   auto plugin = crypto_secrets::plugin{};
   auto runtime = forge::asio::runtime{};
   BOOST_CHECK_THROW(forge::asio::blocking::run(
                        runtime, plugin.configure(forge::config::component_view{document, crypto_secrets_section})),
                     crypto_secrets::exceptions::invalid_secret);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(crypto_secrets_config_value_can_be_delivered_by_forge_env_source_adapter) try {
   auto plugin = crypto_secrets::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   auto registry = forge::config::component_registry{};
   registry.add(*descriptor);

   const auto document = secrets_config(
      {secret_entry("from-env-adapter", source_value("env-secret"), {"payload.decrypt"}, {"get_bytes"}, true)});
   const auto written = forge::env::write_document(document, registry, {.prefix = "FORGE"});
   BOOST_TEST(written.ok());
   BOOST_TEST(written.text.find("env-secret") == std::string::npos);
   BOOST_TEST(written.text.find("FORGE_SECRET_PROVIDER_SECRETS") == std::string::npos);

   const auto help = forge::program_options::help(registry, "FORGE options");
   BOOST_TEST(help.find("plugins.crypto.secrets.secrets") == std::string::npos);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
