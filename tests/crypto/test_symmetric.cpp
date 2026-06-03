#include <boost/test/unit_test.hpp>
#include <boost/describe.hpp>
#include <fcl/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

import fcl.crypto.aes;
import fcl.crypto.kdf;
import fcl.crypto.random;
import fcl.crypto.types;
import fcl.exceptions;
import fcl.raw.raw;

struct encrypted_record {
   std::uint32_t id = 0;
   std::string name;
};

BOOST_DESCRIBE_STRUCT(encrypted_record, (), (id, name))

BOOST_AUTO_TEST_SUITE(crypto_symmetric)

BOOST_AUTO_TEST_CASE(random_bytes_and_key_have_requested_sizes) try {
   const auto nonce = fcl::crypto::random_bytes(12);
   const auto empty = fcl::crypto::random_bytes(0);
   const auto fixed = fcl::crypto::random_array<24>();
   const auto key = fcl::crypto::generate_aes256_key();

   BOOST_CHECK_EQUAL(nonce.size(), 12U);
   BOOST_CHECK(empty.empty());
   BOOST_CHECK_EQUAL(fixed.size(), 24U);
   BOOST_CHECK_EQUAL(key.bytes.size(), fcl::crypto::aes256_key_size);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hkdf_sha256_derives_requested_material) try {
   const auto material = fcl::crypto::derive_hkdf_sha256(fcl::crypto::hkdf_sha256_request{
       .secret = {'s', 'e', 'c', 'r', 'e', 't'},
       .salt = {'s', 'a', 'l', 't'},
       .info = {'i', 'n', 'f', 'o'},
       .output_size = 48,
   });

   BOOST_CHECK_EQUAL(material.size(), 48U);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(scrypt_derives_requested_material) try {
   const auto material = fcl::crypto::derive_scrypt(fcl::crypto::scrypt_request{
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
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_roundtrips_with_aad) try {
   auto key = fcl::crypto::aes256_key{};
   std::fill(key.bytes.begin(), key.bytes.end(), std::uint8_t{0x42});

   auto encrypted = fcl::crypto::encrypt_aes256_gcm(fcl::crypto::aes256_gcm_encrypt_request{
       .key = key,
       .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
       .plaintext = {'p', 'a', 'y', 'l', 'o', 'a', 'd'},
       .aad = {'m', 'e', 't', 'a'},
   });

   BOOST_CHECK_EQUAL(encrypted.nonce.size(), fcl::crypto::aes_gcm_nonce_size);
   BOOST_CHECK_EQUAL(encrypted.tag.size(), fcl::crypto::aes_gcm_tag_size);
   const auto expected = fcl::crypto::bytes{'p', 'a', 'y', 'l', 'o', 'a', 'd'};
   BOOST_CHECK(encrypted.ciphertext != expected);

   const auto plaintext = fcl::crypto::decrypt_aes256_gcm(fcl::crypto::aes256_gcm_decrypt_request{
       .key = key,
       .encrypted = encrypted,
       .aad = {'m', 'e', 't', 'a'},
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), expected.begin(), expected.end());
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_rejects_bad_tag) try {
   const auto key = fcl::crypto::generate_aes256_key();
   auto encrypted = fcl::crypto::encrypt_aes256_gcm(fcl::crypto::aes256_gcm_encrypt_request{
       .key = key,
       .nonce = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
       .plaintext = {'p', 'a', 'y', 'l', 'o', 'a', 'd'},
       .aad = {'m', 'e', 't', 'a'},
   });

   encrypted.tag.front() ^= std::uint8_t{0x01};

   const auto decrypt_with_bad_tag = [&] {
      (void)fcl::crypto::decrypt_aes256_gcm(fcl::crypto::aes256_gcm_decrypt_request{
          .key = key,
          .encrypted = encrypted,
          .aad = {'m', 'e', 't', 'a'},
      });
   };

   BOOST_CHECK_EXCEPTION(decrypt_with_bad_tag(), fcl::crypto::aes::exceptions::authentication_failed,
                         [](const fcl::crypto::aes::exceptions::authentication_failed& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.aes"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_encoder_matches_one_shot_chunks) try {
   const auto key = fcl::crypto::generate_aes256_key();
   const auto nonce = fcl::crypto::bytes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
   const auto aad = fcl::crypto::bytes{'m', 'e', 't', 'a'};
   const auto plaintext = fcl::crypto::bytes{'s', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g'};

   auto streaming_ciphertext = fcl::crypto::bytes{};
   auto encoder = fcl::crypto::aes256_gcm_encoder{fcl::crypto::aes256_gcm_encoder_options{
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

   const auto one_shot = fcl::crypto::encrypt_aes256_gcm({
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
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_encoder_accepts_raw_pack) try {
   const auto key = fcl::crypto::generate_aes256_key();
   const auto nonce = fcl::crypto::bytes{11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
   const auto aad = fcl::crypto::bytes{'r', 'a', 'w'};
   const auto value = encrypted_record{.id = 42, .name = "packed"};

   auto streaming_ciphertext = fcl::crypto::bytes{};
   auto encoder = fcl::crypto::aes256_gcm_encoder{fcl::crypto::aes256_gcm_encoder_options{
       .key = key,
       .nonce = nonce,
       .aad = aad,
       .ciphertext_sink =
           [&](std::span<const std::uint8_t> chunk) {
              streaming_ciphertext.insert(streaming_ciphertext.end(), chunk.begin(), chunk.end());
           },
   }};

   fcl::raw::pack(encoder, value);
   const auto streaming_auth = encoder.finalize();

   const auto packed = fcl::raw::pack(value);
   const auto one_shot = fcl::crypto::encrypt_aes256_gcm({
       .key = key,
       .nonce = nonce,
       .plaintext = fcl::crypto::bytes{packed.begin(), packed.end()},
       .aad = aad,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_ciphertext.begin(), streaming_ciphertext.end(), one_shot.ciphertext.begin(),
                                 one_shot.ciphertext.end());
   BOOST_CHECK_EQUAL_COLLECTIONS(streaming_auth.tag.begin(), streaming_auth.tag.end(), one_shot.tag.begin(),
                                 one_shot.tag.end());
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_gcm_streaming_decoder_emits_provisional_plaintext_until_finalize) try {
   const auto key = fcl::crypto::generate_aes256_key();
   const auto aad = fcl::crypto::bytes{'m', 'e', 't', 'a'};
   const auto expected = fcl::crypto::bytes{'p', 'r', 'o', 'v', 'i', 's', 'i', 'o', 'n', 'a', 'l'};
   auto encrypted = fcl::crypto::encrypt_aes256_gcm({
       .key = key,
       .nonce = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144},
       .plaintext = expected,
       .aad = aad,
   });

   auto provisional = fcl::crypto::bytes{};
   auto decoder = fcl::crypto::aes256_gcm_decoder{fcl::crypto::aes256_gcm_decoder_options{
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
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(aes256_cbc_roundtrips_compatibility_payload) try {
   auto key = fcl::crypto::aes256_key{};
   std::fill(key.bytes.begin(), key.bytes.end(), std::uint8_t{0x24});

   auto encrypted = fcl::crypto::encrypt_aes256_cbc(fcl::crypto::aes256_cbc_encrypt_request{
       .key = key,
       .iv = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
       .plaintext = {'c', 'o', 'm', 'p', 'a', 't'},
   });

   BOOST_CHECK_EQUAL(encrypted.iv.size(), fcl::crypto::aes_cbc_iv_size);
   const auto expected = fcl::crypto::bytes{'c', 'o', 'm', 'p', 'a', 't'};
   BOOST_CHECK(encrypted.ciphertext != expected);

   const auto plaintext = fcl::crypto::decrypt_aes256_cbc(fcl::crypto::aes256_cbc_decrypt_request{
       .key = key,
       .encrypted = encrypted,
   });

   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), expected.begin(), expected.end());
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
