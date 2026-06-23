#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <forge/exceptions/macros.hpp>

import forge.crypto.base58;
import forge.crypto.types;
import forge.exceptions;

BOOST_AUTO_TEST_SUITE(base58)

BOOST_AUTO_TEST_CASE(base58_byte_api_uses_bitcoin_alphabet_vectors) try {
   const auto hello = forge::crypto::bytes{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
   BOOST_CHECK_EQUAL(forge::crypto::base58_encode(hello), "StV1DL6CwTryKyV");

   const auto leading_zeroes = forge::crypto::bytes{0, 0, 0, 1};
   BOOST_CHECK_EQUAL(forge::crypto::base58_encode(leading_zeroes), "1112");

   auto decoded = forge::crypto::base58_decode("StV1DL6CwTryKyV");
   BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(), hello.begin(), hello.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base58_legacy_char_api_stays_compatible) try {
   const std::vector<char> hello{'h', 'e', 'l', 'l', 'o'};
   BOOST_CHECK_EQUAL(forge::crypto::to_base58(hello, [] {}), "Cn8eVZg");

   auto decoded = forge::crypto::from_base58("Cn8eVZg");
   BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(), hello.begin(), hello.end());
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base58_rejects_invalid_characters) try {
   BOOST_CHECK_EXCEPTION((void)forge::crypto::base58_decode("0OIl"),
                         forge::crypto::base58::exceptions::invalid_character,
                         [](const forge::crypto::base58::exceptions::invalid_character& error) {
      return error.code().category().name() == std::string_view{"forge.crypto.base58"};
   });
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
