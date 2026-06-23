#include <boost/test/unit_test.hpp>
#include <forge/exceptions/macros.hpp>

import forge.crypto.base64;
import forge.exceptions;

using namespace forge;
using namespace std::literals;

BOOST_AUTO_TEST_SUITE(base64)

BOOST_AUTO_TEST_CASE(base64enc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc/tPUB+n5h"s;

   BOOST_CHECK_EQUAL(expected_output, forge::crypto::base64_encode(input));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urlenc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc_tPUB-n5h"s;

   BOOST_CHECK_EQUAL(expected_output, forge::crypto::base64url_encode(input));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = forge::crypto::base64_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urldec) try {
   auto input = "YWJjMTIzJCYoKSc_tPUB-n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = forge::crypto::base64url_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_extraequals) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h========="s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = forge::crypto::base64_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_bad_stuff) try {
   auto input = "YWJjMTIzJCYoKSc/tPU$B+n5h="s;

   BOOST_CHECK_EXCEPTION(forge::crypto::base64_decode(input), forge::exceptions::context_error, [](const forge::exceptions::context_error& e) {
      return std::string(e.what()).find("encountered non-base64 character") != std::string::npos;
   });
}
FORGE_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
