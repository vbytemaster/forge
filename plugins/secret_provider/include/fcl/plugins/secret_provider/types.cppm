module;

#include <boost/describe.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

export module fcl.plugins.secret_provider.types;

import fcl.crypto.types;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::plugins::secret_provider {

inline constexpr auto default_max_plaintext_bytes = std::uint64_t{1'048'576};
inline constexpr auto aes_update_bytes_ceiling = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
inline constexpr auto max_aad_bytes_ceiling = aes_update_bytes_ceiling;
inline constexpr auto default_max_aad_bytes = std::uint64_t{1'048'576};
inline constexpr auto encrypted_file_v1_magic_bytes = std::uint64_t{8};
inline constexpr auto encrypted_file_v1_u64_fields = std::uint64_t{7};
inline constexpr auto encrypted_file_default_salt_bytes = std::uint64_t{16};
inline constexpr auto encrypted_file_default_nonce_bytes = std::uint64_t{12};
inline constexpr auto encrypted_file_aes_gcm_tag_bytes = std::uint64_t{16};
inline constexpr auto encrypted_file_default_container_overhead_bytes =
   encrypted_file_v1_magic_bytes + encrypted_file_v1_u64_fields * std::uint64_t{8} +
   encrypted_file_default_salt_bytes + encrypted_file_default_nonce_bytes + encrypted_file_aes_gcm_tag_bytes;
inline constexpr auto default_max_ciphertext_bytes =
   default_max_plaintext_bytes + encrypted_file_default_container_overhead_bytes;
inline constexpr auto default_scrypt_n = std::uint64_t{16'384};
inline constexpr auto default_scrypt_r = std::uint64_t{8};
inline constexpr auto default_scrypt_p = std::uint64_t{1};
inline constexpr auto default_scrypt_max_memory_bytes = std::uint64_t{32ULL * 1024ULL * 1024ULL};
inline constexpr auto default_encrypted_file_max_scrypt_n = default_scrypt_n;
inline constexpr auto default_encrypted_file_max_scrypt_r = default_scrypt_r;
inline constexpr auto default_encrypted_file_max_scrypt_p = default_scrypt_p;
inline constexpr auto default_encrypted_file_max_scrypt_memory_bytes = default_scrypt_max_memory_bytes;

enum class secret_kind {
   bytes,
   symmetric_key,
};

enum class source_type {
   value,
   file,
   encrypted_file,
};

enum class encoding {
   raw,
   hex,
   base64,
};

enum class operation {
   get_bytes,
   derive_hkdf_sha256,
   encrypt_aes_gcm,
   decrypt_aes_gcm,
};

struct secret_source {
   source_type type = source_type::value;
   encoding encoding = encoding::raw;
   std::string value;
   std::string path;
   std::string passphrase_value;
   std::string passphrase_file;
};

struct secret_entry {
   std::string id;
   secret_kind kind = secret_kind::symmetric_key;
   secret_source source;
   std::vector<std::string> purposes;
   std::vector<operation> operations;
   bool allow_raw_export = false;
   std::uint64_t max_plaintext_bytes = 0;
   std::uint64_t max_ciphertext_bytes = 0;
   std::uint64_t max_aad_bytes = 0;
};

struct config {
   std::vector<secret_entry> secrets;
   std::uint64_t default_max_plaintext_bytes = fcl::plugins::secret_provider::default_max_plaintext_bytes;
   std::uint64_t default_max_ciphertext_bytes = fcl::plugins::secret_provider::default_max_ciphertext_bytes;
   std::uint64_t default_max_aad_bytes = fcl::plugins::secret_provider::default_max_aad_bytes;
   std::uint64_t encrypted_file_max_scrypt_n = fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_n;
   std::uint64_t encrypted_file_max_scrypt_r = fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_r;
   std::uint64_t encrypted_file_max_scrypt_p = fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_p;
   std::uint64_t encrypted_file_max_scrypt_memory_bytes =
      fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_memory_bytes;
};

struct query {};

struct secret_summary {
   std::string id;
   secret_kind kind = secret_kind::symmetric_key;
   std::vector<std::string> purposes;
   std::vector<operation> operations;
   bool allow_raw_export = false;
};

struct snapshot {
   std::uint64_t configured_secrets = 0;
   bool stopping = false;
   std::vector<secret_summary> secrets;
};

struct get_request {
   std::string secret_id;
   std::string purpose;
};

struct get_result {
   std::string secret_id;
   fcl::crypto::bytes bytes;
};

struct derive_request {
   std::string secret_id;
   std::string purpose;
   fcl::crypto::bytes salt;
   fcl::crypto::bytes info;
   std::uint64_t output_size = 32;
};

struct derive_result {
   std::string secret_id;
   fcl::crypto::bytes bytes;
};

struct aead_encrypt_request {
   std::string secret_id;
   std::string purpose;
   fcl::crypto::bytes nonce;
   fcl::crypto::bytes plaintext;
   fcl::crypto::bytes aad;
};

struct aead_encrypt_result {
   std::string secret_id;
   fcl::crypto::bytes nonce;
   fcl::crypto::bytes tag;
   fcl::crypto::bytes ciphertext;
};

struct aead_decrypt_request {
   std::string secret_id;
   std::string purpose;
   fcl::crypto::bytes nonce;
   fcl::crypto::bytes tag;
   fcl::crypto::bytes ciphertext;
   fcl::crypto::bytes aad;
};

struct aead_decrypt_result {
   std::string secret_id;
   fcl::crypto::bytes plaintext;
};

struct encrypted_file_encrypt_request {
   fcl::crypto::bytes plaintext;
   std::string passphrase;
   fcl::crypto::bytes salt;
   fcl::crypto::bytes nonce;
   std::uint64_t scrypt_n = default_scrypt_n;
   std::uint64_t scrypt_r = default_scrypt_r;
   std::uint64_t scrypt_p = default_scrypt_p;
   std::uint64_t scrypt_max_memory_bytes = default_scrypt_max_memory_bytes;
};

struct encrypted_file_decrypt_limits {
   std::uint64_t max_plaintext_bytes = default_max_plaintext_bytes;
   std::uint64_t max_scrypt_n = default_encrypted_file_max_scrypt_n;
   std::uint64_t max_scrypt_r = default_encrypted_file_max_scrypt_r;
   std::uint64_t max_scrypt_p = default_encrypted_file_max_scrypt_p;
   std::uint64_t max_scrypt_memory_bytes = default_encrypted_file_max_scrypt_memory_bytes;
};

[[nodiscard]] fcl::crypto::bytes encrypt_secret_file(encrypted_file_encrypt_request request);
[[nodiscard]] fcl::crypto::bytes decrypt_secret_file(const fcl::crypto::bytes& container,
                                                     const std::string& passphrase,
                                                     encrypted_file_decrypt_limits limits);

BOOST_DESCRIBE_ENUM(secret_kind, bytes, symmetric_key)
BOOST_DESCRIBE_ENUM(source_type, value, file, encrypted_file)
BOOST_DESCRIBE_ENUM(encoding, raw, hex, base64)
BOOST_DESCRIBE_ENUM(operation, get_bytes, derive_hkdf_sha256, encrypt_aes_gcm, decrypt_aes_gcm)
BOOST_DESCRIBE_STRUCT(secret_source, (), (type, encoding, value, path, passphrase_value, passphrase_file))
BOOST_DESCRIBE_STRUCT(secret_entry,
                      (),
                      (id,
                       kind,
                       source,
                       purposes,
                       operations,
                       allow_raw_export,
                       max_plaintext_bytes,
                       max_ciphertext_bytes,
                       max_aad_bytes))
BOOST_DESCRIBE_STRUCT(config,
                      (),
                      (secrets,
                       default_max_plaintext_bytes,
                       default_max_ciphertext_bytes,
                       default_max_aad_bytes,
                       encrypted_file_max_scrypt_n,
                       encrypted_file_max_scrypt_r,
                       encrypted_file_max_scrypt_p,
                       encrypted_file_max_scrypt_memory_bytes))
BOOST_DESCRIBE_STRUCT(query, (), ())
BOOST_DESCRIBE_STRUCT(secret_summary, (), (id, kind, purposes, operations, allow_raw_export))
BOOST_DESCRIBE_STRUCT(snapshot, (), (configured_secrets, stopping, secrets))
BOOST_DESCRIBE_STRUCT(get_request, (), (secret_id, purpose))
BOOST_DESCRIBE_STRUCT(get_result, (), (secret_id, bytes))
BOOST_DESCRIBE_STRUCT(derive_request, (), (secret_id, purpose, salt, info, output_size))
BOOST_DESCRIBE_STRUCT(derive_result, (), (secret_id, bytes))
BOOST_DESCRIBE_STRUCT(aead_encrypt_request, (), (secret_id, purpose, nonce, plaintext, aad))
BOOST_DESCRIBE_STRUCT(aead_encrypt_result, (), (secret_id, nonce, tag, ciphertext))
BOOST_DESCRIBE_STRUCT(aead_decrypt_request, (), (secret_id, purpose, nonce, tag, ciphertext, aad))
BOOST_DESCRIBE_STRUCT(aead_decrypt_result, (), (secret_id, plaintext))
BOOST_DESCRIBE_STRUCT(encrypted_file_encrypt_request,
                      (),
                      (plaintext, passphrase, salt, nonce, scrypt_n, scrypt_r, scrypt_p, scrypt_max_memory_bytes))
BOOST_DESCRIBE_STRUCT(encrypted_file_decrypt_limits,
                      (),
                      (max_plaintext_bytes, max_scrypt_n, max_scrypt_r, max_scrypt_p, max_scrypt_memory_bytes))

} // namespace fcl::plugins::secret_provider

export template <> struct fcl::schema::rules<fcl::plugins::secret_provider::secret_source> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::secret_provider::secret_source> define() {
      auto schema = fcl::schema::object<fcl::plugins::secret_provider::secret_source>();
      schema.field<&fcl::plugins::secret_provider::secret_source::type>("type").description("Secret source type");
      schema.field<&fcl::plugins::secret_provider::secret_source::encoding>("encoding")
         .description("Encoding used by value and file sources");
      schema.field<&fcl::plugins::secret_provider::secret_source::value>("value")
         .secret()
         .description("Secret value delivered by FCL config sources");
      schema.field<&fcl::plugins::secret_provider::secret_source::path>("path")
         .description("Secret file or encrypted_file path");
      schema.field<&fcl::plugins::secret_provider::secret_source::passphrase_value>("passphrase-value")
         .secret()
         .description("Passphrase for encrypted_file source");
      schema.field<&fcl::plugins::secret_provider::secret_source::passphrase_file>("passphrase-file")
         .description("Path to passphrase file for encrypted_file source");
      return schema;
   }
};

export template <> struct fcl::schema::rules<fcl::plugins::secret_provider::secret_entry> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::secret_provider::secret_entry> define() {
      auto schema = fcl::schema::object<fcl::plugins::secret_provider::secret_entry>();
      schema.field<&fcl::plugins::secret_provider::secret_entry::id>("id").required().non_empty();
      schema.field<&fcl::plugins::secret_provider::secret_entry::kind>("kind").description("Secret material kind");
      schema.field<&fcl::plugins::secret_provider::secret_entry::source>("source").required().secret();
      schema.field<&fcl::plugins::secret_provider::secret_entry::purposes>("purposes").min_items(1).each_non_empty();
      schema.field<&fcl::plugins::secret_provider::secret_entry::operations>("operations").min_items(1);
      schema.field<&fcl::plugins::secret_provider::secret_entry::allow_raw_export>("allow-raw-export")
         .default_value(false);
      schema.field<&fcl::plugins::secret_provider::secret_entry::max_plaintext_bytes>("max-plaintext-bytes")
         .range(0, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      schema.field<&fcl::plugins::secret_provider::secret_entry::max_ciphertext_bytes>("max-ciphertext-bytes")
         .range(0, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      schema.field<&fcl::plugins::secret_provider::secret_entry::max_aad_bytes>("max-aad-bytes")
         .range(0, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      return schema;
   }
};

export template <> struct fcl::schema::rules<fcl::plugins::secret_provider::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::secret_provider::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::secret_provider::config>();
      schema.field<&fcl::plugins::secret_provider::config::secrets>("secrets")
         .items<fcl::plugins::secret_provider::secret_entry>()
         .secret()
         .unique_by<&fcl::plugins::secret_provider::secret_entry::id>();
      schema.field<&fcl::plugins::secret_provider::config::default_max_plaintext_bytes>("default-max-plaintext-bytes")
         .default_value(fcl::plugins::secret_provider::default_max_plaintext_bytes)
         .range(1, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      schema.field<&fcl::plugins::secret_provider::config::default_max_ciphertext_bytes>("default-max-ciphertext-bytes")
         .default_value(fcl::plugins::secret_provider::default_max_ciphertext_bytes)
         .range(1, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      schema.field<&fcl::plugins::secret_provider::config::default_max_aad_bytes>("default-max-aad-bytes")
         .default_value(fcl::plugins::secret_provider::default_max_aad_bytes)
         .range(1, fcl::plugins::secret_provider::aes_update_bytes_ceiling);
      schema.field<&fcl::plugins::secret_provider::config::encrypted_file_max_scrypt_n>("encrypted-file-max-scrypt-n")
         .default_value(fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_n)
         .range(1, 1ULL << 32);
      schema.field<&fcl::plugins::secret_provider::config::encrypted_file_max_scrypt_r>("encrypted-file-max-scrypt-r")
         .default_value(fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_r)
         .range(1, 1ULL << 32);
      schema.field<&fcl::plugins::secret_provider::config::encrypted_file_max_scrypt_p>("encrypted-file-max-scrypt-p")
         .default_value(fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_p)
         .range(1, 1ULL << 32);
      schema
         .field<&fcl::plugins::secret_provider::config::encrypted_file_max_scrypt_memory_bytes>(
            "encrypted-file-max-scrypt-memory-bytes")
         .default_value(fcl::plugins::secret_provider::default_encrypted_file_max_scrypt_memory_bytes)
         .range(1, 1ULL << 32);
      return schema;
   }
};
