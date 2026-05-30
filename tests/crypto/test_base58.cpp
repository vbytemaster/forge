#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fcl/exception/macros.hpp>

import fcl.crypto.base58;
import fcl.crypto.types;
import fcl.exception.exception;

BOOST_AUTO_TEST_SUITE(base58)

BOOST_AUTO_TEST_CASE(base58_byte_api_uses_bitcoin_alphabet_vectors) try {
   const auto hello = fcl::crypto::bytes{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
   BOOST_CHECK_EQUAL(fcl::crypto::base58_encode(hello), "StV1DL6CwTryKyV");

   const auto leading_zeroes = fcl::crypto::bytes{0, 0, 0, 1};
   BOOST_CHECK_EQUAL(fcl::crypto::base58_encode(leading_zeroes), "1112");

   auto decoded = fcl::crypto::base58_decode("StV1DL6CwTryKyV");
   BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(), hello.begin(), hello.end());
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base58_legacy_char_api_stays_compatible) try {
   const std::vector<char> hello{'h', 'e', 'l', 'l', 'o'};
   BOOST_CHECK_EQUAL(fcl::crypto::to_base58(hello, [] {}), "Cn8eVZg");

   auto decoded = fcl::crypto::from_base58("Cn8eVZg");
   BOOST_CHECK_EQUAL_COLLECTIONS(decoded.begin(), decoded.end(), hello.begin(), hello.end());
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base58_rejects_invalid_characters) try {
   BOOST_CHECK_EXCEPTION((void)fcl::crypto::base58_decode("0OIl"),
                         fcl::crypto::base58::exceptions::invalid_character,
                         [](const fcl::crypto::base58::exceptions::invalid_character& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.base58"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
