#include <boost/test/unit_test.hpp>
#include <fcl/exception/macros.hpp>
#include <span>
#include <string>
#include <vector>

import fcl.crypto.asymmetric;
import fcl.crypto.secp256k1;
import fcl.crypto.p256;
import fcl.crypto.ed25519;
import fcl.crypto.rsa;
import fcl.crypto.x25519;
import fcl.crypto.chacha20_poly1305;
import fcl.crypto.sha256;
import fcl.core.utility;
import fcl.exception.exception;

using namespace fcl::crypto;
using namespace fcl::crypto::asymmetric;
using namespace fcl;

BOOST_AUTO_TEST_SUITE(cypher_suites)
BOOST_AUTO_TEST_CASE(test_k1) try {
   auto private_key_string = std::string("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
   auto expected_public_key = std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV");
   auto test_private_key = encoding::eos().parse_private(private_key_string);
   auto test_public_key = test_private_key.get_public_key();

   BOOST_CHECK_EQUAL(private_key_string, encoding::eos().format(test_private_key));
   BOOST_CHECK_EQUAL(expected_public_key, encoding::eos().format(test_public_key));
   BOOST_CHECK(encoding::fcl().format(test_public_key).starts_with("PUB_SECP256K1_"));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_eos_encoding) try {
   auto private_key_string = std::string("PVT_R1_iyQmnyPEGvFd8uffnk152WC2WryBjgTrg22fXQryuGL9mU6qW");
   auto expected_public_key = std::string("PUB_R1_6EPHFSKVYHBjQgxVGQPrwCxTg7BbZ69H9i4gztN9deKTEXYne4");
   auto test_private_key = encoding::eos().parse_private(private_key_string);
   auto test_public_key = test_private_key.get_public_key();

   BOOST_CHECK_EQUAL(private_key_string, encoding::eos().format(test_private_key));
   BOOST_CHECK_EQUAL(expected_public_key, encoding::eos().format(test_public_key));
   BOOST_CHECK(encoding::fcl().format(test_public_key).starts_with("PUB_P256_"));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_secp256k1_recovery) try {
   const auto payload = std::vector<std::uint8_t>{'T', 'e', 's', 't'};
   auto digest = sha256::hash(std::span<const std::uint8_t>{payload});
   auto key = private_key::generate<secp256k1::private_key_shim>();
   auto pub = key.get_public_key();
   auto sig = key.sign(payload);

   auto recovered_pub = public_key(sig, digest);
   std::cout << recovered_pub.to_string({}) << std::endl;

   BOOST_CHECK_EQUAL(recovered_pub.to_string({}), pub.to_string({}));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_recovery) try {
   const auto payload = std::vector<std::uint8_t>{'T', 'e', 's', 't'};
   auto digest = sha256::hash(std::span<const std::uint8_t>{payload});
   auto key = private_key::generate<p256::private_key_shim>();
   auto pub = key.get_public_key();
   auto sig = key.sign(payload);

   auto recovered_pub = public_key(sig, digest);
   std::cout << recovered_pub.to_string({}) << std::endl;

   BOOST_CHECK_EQUAL(recovered_pub.to_string({}), pub.to_string({}));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_k1_recyle) try {
   auto key = private_key::generate<secp256k1::private_key_shim>();
   auto pub = key.get_public_key();
   auto pub_str = pub.to_string({});
   auto recycled_pub = public_key(pub_str);

   std::cout << pub.to_string({}) << " -> " << recycled_pub.to_string({}) << std::endl;

   BOOST_CHECK_EQUAL(pub.to_string({}), recycled_pub.to_string({}));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(test_p256_recycle) try {
   auto key = private_key::generate<p256::private_key_shim>();
   auto pub = key.get_public_key();
   auto pub_str = pub.to_string({});
   auto recycled_pub = public_key(pub_str);

   std::cout << pub.to_string({}) << " -> " << recycled_pub.to_string({}) << std::endl;

   BOOST_CHECK_EQUAL(pub.to_string({}), recycled_pub.to_string({}));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(generic_sign_verify_all_supported_algorithms) try {
   const auto message = std::vector<std::uint8_t>{'f', 'c', 'l', '-', 'c', 'r', 'y', 'p', 't', 'o'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto keys = std::vector<private_key>{
      private_key::generate<secp256k1::private_key_shim>(),
      private_key::generate<p256::private_key_shim>(),
      private_key::generate<ed25519::private_key_shim>(),
      private_key::generate<rsa::private_key_shim>(),
   };

   for (const auto& key : keys) {
      auto sig = key.sign(message);
      auto pub = key.get_public_key();
      BOOST_CHECK(pub.verify(message, sig));
      BOOST_CHECK(!pub.verify(wrong_message, sig));
      BOOST_CHECK_EQUAL(encoding::fcl().format(pub).substr(0, 4), "PUB_");
      BOOST_CHECK_EQUAL(encoding::fcl().format(sig).substr(0, 4), "SIG_");
   }
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(x25519_key_agreement_roundtrip) try {
   auto alice = x25519::private_key::generate();
   auto bob = x25519::private_key::generate();

   BOOST_CHECK(alice.get_shared_secret(bob.get_public_key()) == bob.get_shared_secret(alice.get_public_key()));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(chacha20_poly1305_authenticates_ciphertext) try {
   auto key = chacha20_poly1305::key{};
   key.fill(7);
   auto nonce = chacha20_poly1305::nonce{};
   nonce.fill(3);
   const auto ad = std::vector<std::uint8_t>{1, 2, 3};
   const auto plaintext = std::vector<std::uint8_t>{4, 5, 6, 7};

   auto encrypted = chacha20_poly1305::encrypt(key, nonce, ad, plaintext);
   auto decrypted = chacha20_poly1305::decrypt(key, nonce, ad, encrypted);
   BOOST_CHECK_EQUAL_COLLECTIONS(plaintext.begin(), plaintext.end(), decrypted.begin(), decrypted.end());

   encrypted.back() ^= 0x01;
   BOOST_CHECK_THROW((void)chacha20_poly1305::decrypt(key, nonce, ad, encrypted), fcl::exception::base);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
