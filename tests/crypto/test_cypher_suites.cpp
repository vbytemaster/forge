#include <boost/test/unit_test.hpp>
#include <fcl/exceptions/macros.hpp>
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
import fcl.exceptions;

using namespace fcl::crypto;
using namespace fcl::crypto::asymmetric;
using namespace fcl;

BOOST_AUTO_TEST_SUITE(cypher_suites)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

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

BOOST_AUTO_TEST_CASE(antelope_encoding_matches_legacy_eos_profile) try {
   const auto message = std::vector<std::uint8_t>{'a', 'n', 't', 'e', 'l', 'o', 'p', 'e'};
   const auto k1 = encoding::eos().parse_private("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
   const auto r1 = encoding::eos().parse_private("PVT_R1_iyQmnyPEGvFd8uffnk152WC2WryBjgTrg22fXQryuGL9mU6qW");

   for (const auto& key : std::vector<private_key>{k1, r1}) {
      const auto public_key = key.get_public_key();
      const auto signature = key.sign(message);

      BOOST_TEST(encoding::antelope().format(key) == encoding::eos().format(key));
      BOOST_TEST(encoding::antelope().format(public_key) == encoding::eos().format(public_key));
      BOOST_TEST(encoding::antelope().format(signature) == encoding::eos().format(signature));
      BOOST_TEST(encoding::antelope().parse_public(encoding::eos().format(public_key)).to_string({}) ==
                 public_key.to_string({}));
      BOOST_TEST(encoding::antelope().parse_signature(encoding::eos().format(signature)).to_string({}) ==
                 signature.to_string({}));
   }
}
FCL_LOG_AND_RETHROW();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

BOOST_AUTO_TEST_CASE(custom_encoding_profile_controls_text_prefixes) try {
   const auto key = private_key::generate<secp256k1::private_key_shim>();
   const auto public_key = key.get_public_key();
   const auto signature = key.sign(std::vector<std::uint8_t>{'s', 'p', 'r', 'i', 'n', 'g'});

   auto profile = text_encoding_profile{
      .id = "spring",
   };
   profile.private_keys.push_back(text_encoding_rule{
      .type = algorithm::secp256k1,
      .text_prefix = "PVT_K1_",
      .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });
   profile.public_keys.push_back(text_encoding_rule{
      .type = algorithm::secp256k1,
      .text_prefix = "SPRING",
      .checksum = {.scheme = checksum_scheme::ripemd160},
   });
   profile.signatures.push_back(text_encoding_rule{
      .type = algorithm::secp256k1,
      .text_prefix = "SIG_K1_",
      .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });

   const auto spring = encoding::custom(profile);
   const auto public_text = spring.format(public_key);
   const auto private_text = spring.format(key);
   const auto signature_text = spring.format(signature);

   BOOST_TEST(public_text.starts_with("SPRING"));
   BOOST_TEST(private_text.starts_with("PVT_K1_"));
   BOOST_TEST(signature_text.starts_with("SIG_K1_"));
   BOOST_TEST(spring.parse_public(public_text).to_string({}) == public_key.to_string({}));
   BOOST_TEST(spring.parse_private(private_text).to_string({}) == key.to_string({}));
   BOOST_TEST(spring.parse_signature(signature_text).to_string({}) == signature.to_string({}));
   BOOST_CHECK_THROW((void)spring.parse_public(encoding::fcl().format(public_key)),
                     asymmetric::exceptions::invalid_key);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(custom_encoding_rejects_unsupported_algorithm_for_profile) try {
   auto profile = text_encoding_profile{.id = "k1-only"};
   profile.public_keys.push_back(text_encoding_rule{
      .type = algorithm::secp256k1,
      .text_prefix = "PUB_K1_",
      .checksum = {.scheme = checksum_scheme::ripemd160_with_text_suffix, .text_suffix = "K1"},
   });

   const auto k1_only = encoding::custom(profile);
   const auto p256_key = private_key::generate<p256::private_key_shim>().get_public_key();
   BOOST_CHECK_THROW((void)k1_only.format(p256_key), asymmetric::exceptions::invalid_options);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(built_in_profiles_cover_common_text_encoding_families) try {
   const auto k1 = private_key::generate<secp256k1::private_key_shim>();
   const auto ed25519 = private_key::generate<ed25519::private_key_shim>();
   const auto message = std::vector<std::uint8_t>{'p', 'r', 'o', 'f', 'i', 'l', 'e'};
   const auto ed25519_signature = ed25519.sign(message);

   const auto bitcoin = encoding::from_profile(profiles::bitcoin());
   const auto wif = bitcoin.format(k1);
   BOOST_TEST(!wif.empty());
   BOOST_TEST(!wif.starts_with("PVT_"));
   BOOST_TEST(bitcoin.parse_private(wif).to_string({}) == k1.to_string({}));
   BOOST_CHECK_THROW((void)bitcoin.format(k1.get_public_key()), asymmetric::exceptions::invalid_options);

   const auto solana = encoding::from_profile(profiles::solana());
   const auto solana_public = solana.format(ed25519.get_public_key());
   const auto solana_private = solana.format(ed25519);
   const auto solana_signature = solana.format(ed25519_signature);
   BOOST_TEST(!solana_public.empty());
   BOOST_TEST(!solana_public.starts_with("PUB_"));
   BOOST_TEST(solana.parse_public(solana_public).to_string({}) == ed25519.get_public_key().to_string({}));
   BOOST_TEST(solana.parse_private(solana_private).to_string({}) == ed25519.to_string({}));
   BOOST_TEST(solana.parse_signature(solana_signature).to_string({}) == ed25519_signature.to_string({}));

   const auto tezos = encoding::from_profile(profiles::tezos());
   const auto tezos_public = tezos.format(ed25519.get_public_key());
   const auto tezos_private = tezos.format(ed25519);
   const auto tezos_signature = tezos.format(ed25519_signature);
   BOOST_TEST(tezos_public.starts_with("edpk"));
   BOOST_TEST(!tezos_private.empty());
   BOOST_TEST(tezos_signature.starts_with("edsig"));
   BOOST_TEST(tezos.parse_public(tezos_public).to_string({}) == ed25519.get_public_key().to_string({}));
   BOOST_TEST(tezos.parse_private(tezos_private).to_string({}) == ed25519.to_string({}));
   BOOST_TEST(tezos.parse_signature(tezos_signature).to_string({}) == ed25519_signature.to_string({}));
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

BOOST_AUTO_TEST_CASE(secp256k1_der_signature_matches_message_api) try {
   const auto message = std::vector<std::uint8_t>{'l', 'i', 'b', 'p', '2', 'p', '-', 'i', 'd'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto key = secp256k1::private_key_shim::generate();
   const auto signature = secp256k1::sign_der(key, message);

   BOOST_TEST(secp256k1::verify_der(key.get_public_key(), message, signature));
   BOOST_TEST(!secp256k1::verify_der(key.get_public_key(), wrong_message, signature));

   auto malformed = signature;
   malformed.front() ^= 0xffU;
   BOOST_CHECK_THROW((void)secp256k1::verify_der(key.get_public_key(), message, malformed),
                     secp256k1::exceptions::invalid_signature);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(p256_der_signature_matches_message_api) try {
   const auto message = std::vector<std::uint8_t>{'l', 'i', 'b', 'p', '2', 'p', '-', 'e', 'c', 'd', 's', 'a'};
   const auto wrong_message = std::vector<std::uint8_t>{'w', 'r', 'o', 'n', 'g'};
   const auto key = p256::private_key_shim::generate();
   const auto signature = p256::sign_der(key, message);

   BOOST_TEST(p256::verify_der(key.get_public_key(), message, signature));
   BOOST_TEST(!p256::verify_der(key.get_public_key(), wrong_message, signature));

   auto malformed = signature;
   malformed.front() ^= 0xffU;
   BOOST_CHECK_THROW((void)p256::verify_der(key.get_public_key(), message, malformed),
                     p256::exceptions::invalid_signature);
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
   BOOST_CHECK_THROW((void)chacha20_poly1305::decrypt(key, nonce, ad, encrypted), fcl::exceptions::base);
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
