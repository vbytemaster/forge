#include <boost/test/unit_test.hpp>
#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

import fcl.crypto.aes;
import fcl.crypto.kdf;
import fcl.crypto.random;
import fcl.crypto.types;
import fcl.exception.exception;

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
} FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hkdf_sha256_derives_requested_material) try {
   const auto material = fcl::crypto::derive_hkdf_sha256(fcl::crypto::hkdf_sha256_request{
      .secret = {'s', 'e', 'c', 'r', 'e', 't'},
      .salt = {'s', 'a', 'l', 't'},
      .info = {'i', 'n', 'f', 'o'},
      .output_size = 48,
   });

   BOOST_CHECK_EQUAL(material.size(), 48U);
} FCL_LOG_AND_RETHROW();

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
} FCL_LOG_AND_RETHROW();

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

   BOOST_CHECK_EQUAL_COLLECTIONS(
      plaintext.begin(),
      plaintext.end(),
      expected.begin(),
      expected.end());
} FCL_LOG_AND_RETHROW();

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

   BOOST_CHECK_EXCEPTION(
      decrypt_with_bad_tag(),
      fcl::crypto::error,
      [](const fcl::crypto::error& error) {
         return error.kind() == fcl::crypto::error_kind::authentication_failed;
      });
} FCL_LOG_AND_RETHROW();

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

   BOOST_CHECK_EQUAL_COLLECTIONS(
      plaintext.begin(),
      plaintext.end(),
      expected.begin(),
      expected.end());
} FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
