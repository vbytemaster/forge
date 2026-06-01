#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fcl/exceptions/macros.hpp>

import fcl.crypto.base32;
import fcl.crypto.types;
import fcl.exceptions;

BOOST_AUTO_TEST_SUITE(base32)

BOOST_AUTO_TEST_CASE(base32_rfc4648_vectors_without_padding) try {
   const auto check = [](std::string input, std::string expected) {
      const auto bytes = fcl::crypto::bytes(input.begin(), input.end());
      BOOST_CHECK_EQUAL(fcl::crypto::base32_encode(bytes), expected);

      auto decoded = fcl::crypto::base32_decode(expected);
      BOOST_CHECK_EQUAL(std::string(decoded.begin(), decoded.end()), input);
   };

   check("", "");
   check("f", "my");
   check("fo", "mzxq");
   check("foo", "mzxw6");
   check("foob", "mzxw6yq");
   check("fooba", "mzxw6ytb");
   check("foobar", "mzxw6ytboi");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base32_decode_accepts_uppercase_and_padding) try {
   auto decoded = fcl::crypto::base32_decode("MZXW6YTBOI======");
   BOOST_CHECK_EQUAL(std::string(decoded.begin(), decoded.end()), "foobar");
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base32_rejects_invalid_characters) try {
   BOOST_CHECK_EXCEPTION((void)fcl::crypto::base32_decode("mzxw6ytb0i"),
                         fcl::crypto::base32::exceptions::invalid_options,
                         [](const fcl::crypto::base32::exceptions::invalid_options& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.base32"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
