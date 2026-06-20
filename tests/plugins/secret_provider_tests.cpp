#include <boost/test/unit_test.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import fcl.api.registry;
import fcl.app.plugin;
import fcl.asio.blocking;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.document;
import fcl.config.value;
import fcl.crypto.aes;
import fcl.crypto.base64;
import fcl.crypto.kdf;
import fcl.crypto.secret_bytes;
import fcl.crypto.types;
import fcl.env;
import fcl.plugins.secret_provider.api;
import fcl.plugins.secret_provider.exceptions;
import fcl.plugins.secret_provider.plugin;
import fcl.plugins.secret_provider.types;
import fcl.program_options;
import fcl.schema.value_kind;

namespace secret_provider = fcl::plugins::secret_provider;

namespace {

[[nodiscard]] fcl::config::value array(std::vector<std::string> values) {
   auto result = fcl::config::value::array_type{};
   for (auto& value : values) {
      result.emplace_back(std::move(value));
   }
   return fcl::config::value{std::move(result)};
}

[[nodiscard]] fcl::config::value source_value(std::string value,
                                              secret_provider::encoding encoding = secret_provider::encoding::raw) {
   auto object = fcl::config::value::object_type{};
   object.emplace("type", fcl::config::value{"value"});
   object.emplace("encoding", fcl::config::value{encoding == secret_provider::encoding::hex ? "hex" :
                                                 encoding == secret_provider::encoding::base64 ? "base64" : "raw"});
   object.emplace("value", fcl::config::value{std::move(value)});
   return fcl::config::value{std::move(object)};
}

[[nodiscard]] fcl::config::value source_file(const std::filesystem::path& path,
                                             secret_provider::encoding encoding = secret_provider::encoding::raw) {
   auto object = fcl::config::value::object_type{};
   object.emplace("type", fcl::config::value{"file"});
   object.emplace("encoding", fcl::config::value{encoding == secret_provider::encoding::hex ? "hex" :
                                                 encoding == secret_provider::encoding::base64 ? "base64" : "raw"});
   object.emplace("path", fcl::config::value{path.string()});
   return fcl::config::value{std::move(object)};
}

[[nodiscard]] fcl::config::value source_encrypted_file(const std::filesystem::path& path, std::string passphrase) {
   auto object = fcl::config::value::object_type{};
   object.emplace("type", fcl::config::value{"encrypted_file"});
   object.emplace("path", fcl::config::value{path.string()});
   object.emplace("passphrase-value", fcl::config::value{std::move(passphrase)});
   return fcl::config::value{std::move(object)};
}

[[nodiscard]] fcl::config::value secret_entry(std::string id,
                                              fcl::config::value source,
                                              std::vector<std::string> purposes,
                                              std::vector<std::string> operations,
                                              bool allow_raw_export = false) {
   auto object = fcl::config::value::object_type{};
   object.emplace("id", fcl::config::value{std::move(id)});
   object.emplace("kind", fcl::config::value{"symmetric_key"});
   object.emplace("source", std::move(source));
   object.emplace("purposes", array(std::move(purposes)));
   object.emplace("operations", array(std::move(operations)));
   object.emplace("allow-raw-export", fcl::config::value{allow_raw_export});
   return fcl::config::value{std::move(object)};
}

[[nodiscard]] fcl::config::document provider_config(std::vector<fcl::config::value> secrets) {
   auto document = fcl::config::document{};
   document.set("secret-provider.secrets", fcl::config::value::array_type(secrets.begin(), secrets.end()));
   return document;
}

void write_secret_file(const std::filesystem::path& path, const fcl::crypto::bytes& value) {
   auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
   out.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
}

void overwrite_u64_le(fcl::crypto::bytes& value, std::size_t offset, std::uint64_t replacement) {
   for (auto i = 0U; i < 8U; ++i) {
      value[offset + i] = static_cast<std::uint8_t>((replacement >> (i * 8U)) & 0xffU);
   }
}

[[nodiscard]] fcl::api::handle<secret_provider::api> configured_api(fcl::asio::runtime& runtime,
                                                                    secret_provider::plugin& plugin,
                                                                    const fcl::config::document& document) {
   fcl::asio::blocking::run(runtime, plugin.configure(fcl::config::component_view{document, "secret-provider"}));
   auto registry = fcl::api::registry{};
   auto installer = fcl::api::installer{registry};
   fcl::asio::blocking::run(runtime, plugin.provide(installer));
   return registry.get<secret_provider::api>(secret_provider::api::ref());
}

[[nodiscard]] fcl::crypto::bytes bytes(std::string_view value) {
   return fcl::crypto::bytes{value.begin(), value.end()};
}

[[nodiscard]] std::string write_temp_file(std::string name, std::string_view value) {
   const auto path = std::filesystem::temp_directory_path() / name;
   auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
   out.write(value.data(), static_cast<std::streamsize>(value.size()));
   return path.string();
}

} // namespace

BOOST_AUTO_TEST_SUITE(secret_provider_tests)

BOOST_AUTO_TEST_CASE(secret_provider_descriptor_redacts_config_and_keeps_api_local) try {
   static_assert(fcl::api::local_interface<secret_provider::api>);
   static_assert(!fcl::api::remote_interface<secret_provider::api>);

   auto plugin = secret_provider::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   BOOST_TEST(descriptor->section == "secret-provider");

   const auto secrets = std::ranges::find_if(descriptor->fields, [](const auto& field) {
      return field.name == "secrets";
   });
   BOOST_REQUIRE(secrets != descriptor->fields.end());
   BOOST_TEST(secrets->secret);
   BOOST_TEST(static_cast<int>(secrets->kind) == static_cast<int>(fcl::schema::value_kind::object_list));

   const auto has_default_field = [&](std::string_view name, std::uint64_t expected) {
      const auto found = std::ranges::find_if(descriptor->fields, [&](const auto& field) {
         return field.name == name;
      });
      BOOST_REQUIRE(found != descriptor->fields.end());
      BOOST_TEST(found->has_default);
      BOOST_TEST(std::get<std::uint64_t>(found->default_value.storage) == expected);
   };
   has_default_field("encrypted-file-max-scrypt-n", secret_provider::default_encrypted_file_max_scrypt_n);
   has_default_field("encrypted-file-max-scrypt-r", secret_provider::default_encrypted_file_max_scrypt_r);
   has_default_field("encrypted-file-max-scrypt-p", secret_provider::default_encrypted_file_max_scrypt_p);
   has_default_field("encrypted-file-max-scrypt-memory-bytes",
                     secret_provider::default_encrypted_file_max_scrypt_memory_bytes);

   auto registry = fcl::config::component_registry{};
   registry.add(*descriptor);
   const auto redacted = fcl::config::redact(
      provider_config({secret_entry("session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"}, true)}),
      registry);
   const auto* value = redacted.try_get("secret-provider.secrets");
   BOOST_REQUIRE(value != nullptr);
   const auto* text = std::get_if<std::string>(&value->storage);
   BOOST_REQUIRE(text != nullptr);
   BOOST_TEST(*text == "<redacted>");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_value_source_denies_raw_export_by_default) try {
   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             provider_config({secret_entry(
                                "session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"})}));

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime,
                                              api->get_bytes(secret_provider::get_request{
                                                 .secret_id = "session",
                                                 .purpose = "payload.decrypt",
                                              })),
                     secret_provider::exceptions::operation_denied);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_value_source_exports_only_when_allowed) try {
   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             provider_config({secret_entry(
                                "session", source_value("super-secret"), {"payload.decrypt"}, {"get_bytes"}, true)}));

   const auto result = fcl::asio::blocking::run(runtime,
                                               api->get_bytes(secret_provider::get_request{
                                                  .secret_id = "session",
                                                  .purpose = "payload.decrypt",
                                               }));

   BOOST_TEST(result.secret_id == "session");
   BOOST_TEST(result.bytes == bytes("super-secret"));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_file_source_loads_bounded_secret_without_env_parser) try {
   const auto path = write_temp_file("fcl-secret-provider-file-source.txt", "file-secret");
   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             provider_config({secret_entry("file",
                                                           source_file(path),
                                                           {"payload.decrypt"},
                                                           {"get_bytes"},
                                                           true)}));

   const auto result = fcl::asio::blocking::run(runtime,
                                               api->get_bytes(secret_provider::get_request{
                                                  .secret_id = "file",
                                                  .purpose = "payload.decrypt",
                                               }));

   BOOST_TEST(result.bytes == bytes("file-secret"));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_hkdf_and_aes_gcm_are_purpose_gated) try {
   const auto key = std::string(32, 'K');
   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             provider_config({secret_entry(
                                "data-key",
                                source_value(key),
                                {"payload.decrypt"},
                                {"derive_hkdf_sha256", "decrypt_aes_gcm"})}));

   const auto derived = fcl::asio::blocking::run(runtime,
                                                api->derive_hkdf_sha256(secret_provider::derive_request{
                                                   .secret_id = "data-key",
                                                   .purpose = "payload.decrypt",
                                                   .salt = bytes("salt"),
                                                   .info = bytes("info"),
                                                   .output_size = 32,
                                                }));
   BOOST_TEST(derived.bytes.size() == 32U);

   auto aes_key = fcl::crypto::make_aes256_key(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(key.data()), key.size()});
   const auto encrypted = fcl::crypto::encrypt_aes256_gcm({
      .key = aes_key,
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .plaintext = bytes("payload"),
      .aad = bytes("aad"),
   });

   const auto decrypted = fcl::asio::blocking::run(runtime,
                                                  api->decrypt_aes_gcm(secret_provider::aead_decrypt_request{
                                                     .secret_id = "data-key",
                                                     .purpose = "payload.decrypt",
                                                     .nonce = encrypted.nonce,
                                                     .tag = encrypted.tag,
                                                     .ciphertext = encrypted.ciphertext,
                                                     .aad = bytes("aad"),
                                                  }));
   BOOST_TEST(decrypted.plaintext == bytes("payload"));

   BOOST_CHECK_THROW(fcl::asio::blocking::run(runtime,
                                              api->derive_hkdf_sha256(secret_provider::derive_request{
                                                 .secret_id = "data-key",
                                                 .purpose = "other",
                                                 .salt = bytes("salt"),
                                                 .info = bytes("info"),
                                                 .output_size = 32,
                                              })),
                     secret_provider::exceptions::purpose_denied);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_encrypted_file_roundtrips_and_rejects_wrong_passphrase) try {
   const auto path = std::filesystem::temp_directory_path() / "fcl-secret-provider-encrypted-source.bin";
   const auto container = secret_provider::encrypt_secret_file(secret_provider::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   auto api = configured_api(runtime,
                             plugin,
                             provider_config({secret_entry("encrypted",
                                                           source_encrypted_file(path, "correct horse battery staple"),
                                                           {"payload.decrypt"},
                                                           {"get_bytes"},
                                                           true)}));
   const auto result = fcl::asio::blocking::run(runtime,
                                               api->get_bytes(secret_provider::get_request{
                                                  .secret_id = "encrypted",
                                                  .purpose = "payload.decrypt",
                                               }));
   BOOST_TEST(result.bytes == bytes("encrypted-secret"));

   auto wrong = secret_provider::plugin{};
   BOOST_CHECK_THROW(
      fcl::asio::blocking::run(
         runtime,
         wrong.configure(fcl::config::component_view{
            provider_config({secret_entry("encrypted",
                                          source_encrypted_file(path, "wrong passphrase"),
                                          {"payload.decrypt"},
                                          {"get_bytes"},
                                          true)}),
            "secret-provider"})),
      secret_provider::exceptions::invalid_secret);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_encrypted_file_rejects_scrypt_params_above_limits_before_kdf) try {
   auto container = secret_provider::encrypt_secret_file(secret_provider::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   overwrite_u64_le(container, 8U, 2048U);

   BOOST_CHECK_THROW((void)secret_provider::decrypt_secret_file(
                        container,
                        "correct horse battery staple",
                        secret_provider::encrypted_file_decrypt_limits{
                           .max_plaintext_bytes = secret_provider::default_max_plaintext_bytes,
                           .max_scrypt_n = 1024,
                           .max_scrypt_r = secret_provider::default_encrypted_file_max_scrypt_r,
                           .max_scrypt_p = secret_provider::default_encrypted_file_max_scrypt_p,
                           .max_scrypt_memory_bytes = secret_provider::default_encrypted_file_max_scrypt_memory_bytes,
                        }),
                     secret_provider::exceptions::invalid_secret);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_config_rejects_encrypted_file_scrypt_params_above_configured_ceilings) try {
   const auto path = std::filesystem::temp_directory_path() / "fcl-secret-provider-encrypted-source-high-scrypt.bin";
   const auto container = secret_provider::encrypt_secret_file(secret_provider::encrypted_file_encrypt_request{
      .plaintext = bytes("encrypted-secret"),
      .passphrase = "correct horse battery staple",
      .salt = bytes("0123456789abcdef"),
      .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .scrypt_n = 1024,
      .scrypt_max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
   });
   write_secret_file(path, container);

   auto document = provider_config({secret_entry("encrypted",
                                                source_encrypted_file(path, "correct horse battery staple"),
                                                {"payload.decrypt"},
                                                {"get_bytes"},
                                                true)});
   document.set("secret-provider.encrypted-file-max-scrypt-n", fcl::config::value{512U});

   auto plugin = secret_provider::plugin{};
   auto runtime = fcl::asio::runtime{};
   BOOST_CHECK_THROW(fcl::asio::blocking::run(
                        runtime, plugin.configure(fcl::config::component_view{document, "secret-provider"})),
                     secret_provider::exceptions::invalid_secret);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_provider_config_value_can_be_delivered_by_fcl_env_source_adapter) try {
   auto plugin = secret_provider::plugin{};
   const auto descriptor = plugin.describe_config();
   BOOST_REQUIRE(descriptor.has_value());
   auto registry = fcl::config::component_registry{};
   registry.add(*descriptor);

   const auto document = provider_config(
      {secret_entry("from-env-adapter", source_value("env-secret"), {"payload.decrypt"}, {"get_bytes"}, true)});
   const auto written = fcl::env::write_document(document, registry, {.prefix = "FCL"});
   BOOST_TEST(written.ok());
   BOOST_TEST(written.text.find("env-secret") == std::string::npos);
   BOOST_TEST(written.text.find("FCL_SECRET_PROVIDER_SECRETS") == std::string::npos);

   const auto help = fcl::program_options::help(registry, "FCL options");
   BOOST_TEST(help.find("secret-provider.secrets") == std::string::npos);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
