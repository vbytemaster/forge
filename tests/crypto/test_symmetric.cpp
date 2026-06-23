#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

import forge.crypto.aes;
import forge.crypto.kdf;
import forge.crypto.random;
import forge.crypto.secret_bytes;
import forge.crypto.types;
import forge.exceptions;
import forge.raw.raw;

struct encrypted_record {
   std::uint32_t id = 0;
   std::string name;
};

BOOST_DESCRIBE_STRUCT(encrypted_record, (), (id, name))

BOOST_AUTO_TEST_SUITE(crypto_symmetric)

BOOST_AUTO_TEST_CASE(random_bytes_and_key_have_requested_sizes) try {
   const auto nonce = forge::crypto::random_bytes(12);
   const auto empty = forge::crypto::random_bytes(0);
   const auto fixed = forge::crypto::random_array<24>();
   const auto key = forge::crypto::generate_aes256_key();

   BOOST_CHECK_EQUAL(nonce.size(), 12U);
   BOOST_CHECK(empty.empty());
   BOOST_CHECK_EQUAL(fixed.size(), 24U);
   BOOST_CHECK_EQUAL(key.bytes.size(), forge::crypto::aes256_key_size);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hkdf_sha256_derives_requested_material) try {
   const auto material = forge::crypto::derive_hkdf_sha256(forge::crypto::hkdf_sha256_request{
       .secret = {'s', 'e', 'c', 'r', 'e', 't'},
       .salt = {'s', 'a', 'l', 't'},
       .info = {'i', 'n', 'f', 'o'},
       .output_size = 48,
   });

   BOOST_CHECK_EQUAL(material.size(), 48U);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hkdf_sha256_accepts_secret_span_without_owning_copy) try {
   auto secret = forge::crypto::secret_bytes{forge::crypto::bytes{'s', 'e', 'c', 'r', 'e', 't'}};
   const auto salt = forge::crypto::bytes{'s', 'a', 'l', 't'};
   const auto info = forge::crypto::bytes{'i', 'n', 'f', 'o'};
   const auto owned = forge::crypto::derive_hkdf_sha256(forge::crypto::hkdf_sha256_request{
       .secret = {'s', 'e', 'c', 'r', 'e', 't'},
       .salt = salt,
       .info = info,
       .output_size = 48,
   });
   const auto from_span = forge::crypto::derive_hkdf_sha256(forge::crypto::hkdf_sha256_span_request{
       .secret = secret.span(),
       .salt = salt,
       .info = info,
       .output_size = 48,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(from_span.begin(), from_span.end(), owned.begin(), owned.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(scrypt_derives_requested_material) try {
   const auto material = forge::crypto::derive_scrypt(forge::crypto::scrypt_request{
       .password = "correct horse battery staple",
       .salt = {'s', 'a', 'l', 't'},
       .n = 1024,
       .r = 8,
       .p = 1,
       .max_memory_bytes = 8ULL * 1024ULL * 1024ULL,
       .output_size = 32,
   });

   BOOST_CHECK_EQUAL(material.size(), 32U);
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(secret_bytes_is_move_only_and_explicitly_clearable) try {
   static_assert(!std::is_copy_constructible_v<forge::crypto::secret_bytes>);
   static_assert(!std::is_copy_assignable_v<forge::crypto::secret_bytes>);
   static_assert(std::is_move_constructible_v<forge::crypto::secret_bytes>);
   static_assert(std::is_move_assignable_v<forge::crypto::secret_bytes>);

   auto secret = forge::crypto::secret_bytes{forge::crypto::bytes{'s', 'e', 'c', 'r', 'e', 't'}};
   const auto expected = forge::crypto::bytes{'s', 'e', 'c', 'r', 'e', 't'};
   BOOST_TEST(secret.size() == 6U);
   BOOST_TEST(!secret.empty());
   BOOST_CHECK_EQUAL_COLLECTIONS(secret.span().begin(), secret.span().end(), expected.begin(), expected.end());

   auto moved = std::move(secret);
   BOOST_TEST(moved.size() == 6U);
   BOOST_TEST(secret.empty());

   moved.clear();
   BOOST_TEST(moved.empty());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_roundtrips_with_aad) try {
   auto key = forge::crypto::aes256_key{};
   std::fill(key.bytes.begin(), key.bytes.end(), std::uint8_t{0x42});

   auto encrypted = forge::crypto::encrypt_aes256_gcm(forge::crypto::aes256_gcm_encrypt_request{
       .key = key,
       .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
       .plaintext = {'p', 'a', 'y', 'l', 'o', 'a', 'd'},
       .aad = {'m', 'e', 't', 'a'},
   });

   BOOST_CHECK_EQUAL(encrypted.nonce.size(), forge::crypto::aes_gcm_nonce_size);
   BOOST_CHECK_EQUAL(encrypted.tag.size(), forge::crypto::aes_gcm_tag_size);
   const auto expected = forge::crypto::bytes{'p', 'a', 'y', 'l', 'o', 'a', 'd'};
   BOOST_CHECK(encrypted.ciphertext != expected);

   const auto plaintext = forge::crypto::decrypt_aes256_gcm(forge::crypto::aes256_gcm_decrypt_request{
       .key = key,
       .encrypted = encrypted,
       .aad = {'m', 'e', 't', 'a'},
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), expected.begin(), expected.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_rejects_bad_tag) try {
   const auto key = forge::crypto::generate_aes256_key();
   auto encrypted = forge::crypto::encrypt_aes256_gcm(forge::crypto::aes256_gcm_encrypt_request{
       .key = key,
       .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
       .plaintext = {'p', 'a', 'y', 'l', 'o', 'a', 'd'},
       .aad = {'m', 'e', 't', 'a'},
   });

   encrypted.tag.front() ^= std::uint8_t{0x01};

   const auto decrypt_with_bad_tag = [&] {
      (void)forge::crypto::decrypt_aes256_gcm(forge::crypto::aes256_gcm_decrypt_request{
          .key = key,
          .encrypted = encrypted,
          .aad = {'m', 'e', 't', 'a'},
      });
   };

   BOOST_CHECK_EXCEPTION(decrypt_with_bad_tag(), forge::crypto::aes::exceptions::authentication_failed,
                         [](const forge::crypto::aes::exceptions::authentication_failed& error) {
      return error.code().category().name() == std::string_view{"forge.crypto.aes"};
   });
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_encoder_matches_one_shot_chunks) try {
   const auto key = forge::crypto::generate_aes256_key();
   const auto nonce = forge::crypto::bytes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
   const auto aad = forge::crypto::bytes{'m', 'e', 't', 'a'};
   const auto plaintext = forge::crypto::bytes{'s', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g'};

   auto streaming_ciphertext = forge::crypto::bytes{};
   auto encoder = forge::crypto::aes256_gcm_encoder{forge::crypto::aes256_gcm_encoder_options{
       .key = key,
       .nonce = nonce,
       .aad = aad,
       .ciphertext_sink =
           [&](std::span<const std::uint8_t> chunk) {
              streaming_ciphertext.insert(streaming_ciphertext.end(), chunk.begin(), chunk.end());
           },
   }};

   encoder.write(std::span<const std::uint8_t>{plaintext.data(), 3});
   encoder.write(std::span<const std::uint8_t>{plaintext.data() + 3, plaintext.size() - 3});
   const auto streaming_auth = encoder.finalize();

   const auto one_shot = forge::crypto::encrypt_aes256_gcm({
       .key = key,
       .nonce = nonce,
       .plaintext = plaintext,
       .aad = aad,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_ciphertext.begin(), streaming_ciphertext.end(), one_shot.ciphertext.begin(),
                                 one_shot.ciphertext.end());
   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_auth.tag.begin(), streaming_auth.tag.end(), one_shot.tag.begin(),
                                 one_shot.tag.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_encoder_accepts_raw_pack) try {
   const auto key = forge::crypto::generate_aes256_key();
   const auto nonce = forge::crypto::bytes{11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
   const auto aad = forge::crypto::bytes{'r', 'a', 'w'};
   const auto value = encrypted_record{.id = 42, .name = "packed"};

   auto streaming_ciphertext = forge::crypto::bytes{};
   auto encoder = forge::crypto::aes256_gcm_encoder{forge::crypto::aes256_gcm_encoder_options{
       .key = key,
       .nonce = nonce,
       .aad = aad,
       .ciphertext_sink =
           [&](std::span<const std::uint8_t> chunk) {
              streaming_ciphertext.insert(streaming_ciphertext.end(), chunk.begin(), chunk.end());
           },
   }};

   forge::raw::pack(encoder, value);
   const auto streaming_auth = encoder.finalize();

   const auto packed = forge::raw::pack(value);
   const auto one_shot = forge::crypto::encrypt_aes256_gcm({
       .key = key,
       .nonce = nonce,
       .plaintext = forge::crypto::bytes{packed.begin(), packed.end()},
       .aad = aad,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_ciphertext.begin(), streaming_ciphertext.end(), one_shot.ciphertext.begin(),
                                 one_shot.ciphertext.end());
   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_auth.tag.begin(), streaming_auth.tag.end(), one_shot.tag.begin(),
                                 one_shot.tag.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_decoder_emits_provisional_plaintext_until_finalize) try {
   const auto key = forge::crypto::generate_aes256_key();
   const auto aad = forge::crypto::bytes{'m', 'e', 't', 'a'};
   const auto expected = forge::crypto::bytes{'p', 'r', 'o', 'v', 'i', 's', 'i', 'o', 'n', 'a', 'l'};
   auto encrypted = forge::crypto::encrypt_aes256_gcm({
       .key = key,
       .nonce = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144},
       .plaintext = expected,
       .aad = aad,
   });

   auto provisional = forge::crypto::bytes{};
   auto decoder = forge::crypto::aes256_gcm_decoder{forge::crypto::aes256_gcm_decoder_options{
       .key = key,
       .nonce = encrypted.nonce,
       .tag = encrypted.tag,
       .aad = aad,
       .plaintext_sink =
           [&](std::span<const std::uint8_t> chunk) {
              provisional.insert(provisional.end(), chunk.begin(), chunk.end());
           },
   }};

   decoder.write(std::span<const std::uint8_t>{encrypted.ciphertext.data(), 4});
   BOOST_CHECK(!provisional.empty());
   decoder.write(std::span<const std::uint8_t>{encrypted.ciphertext.data() + 4, encrypted.ciphertext.size() - 4});
   decoder.finalize();

   BOOST_CHECK_EQUAL_COLLECTIONS(provisional.begin(), provisional.end(), expected.begin(), expected.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_cbc_roundtrips_compatibility_payload) try {
   auto key = forge::crypto::aes256_key{};
   std::fill(key.bytes.begin(), key.bytes.end(), std::uint8_t{0x24});

   auto encrypted = forge::crypto::encrypt_aes256_cbc(forge::crypto::aes256_cbc_encrypt_request{
       .key = key,
       .iv = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
       .plaintext = {'c', 'o', 'm', 'p', 'a', 't'},
   });

   BOOST_CHECK_EQUAL(encrypted.iv.size(), forge::crypto::aes_cbc_iv_size);
   const auto expected = forge::crypto::bytes{'c', 'o', 'm', 'p', 'a', 't'};
   BOOST_CHECK(encrypted.ciphertext != expected);

   const auto plaintext = forge::crypto::decrypt_aes256_cbc(forge::crypto::aes256_cbc_decrypt_request{
       .key = key,
       .encrypted = encrypted,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), expected.begin(), expected.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
