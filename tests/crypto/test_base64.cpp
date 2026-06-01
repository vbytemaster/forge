#include <boost/test/unit_test.hpp>
#include <fcl/exceptions/macros.hpp>

import fcl.crypto.base64;
import fcl.exceptions;

using namespace fcl;
using namespace std::literals;

BOOST_AUTO_TEST_SUITE(base64)

BOOST_AUTO_TEST_CASE(base64enc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc/tPUB+n5h"s;

   BOOST_CHECK_EQUAL(expected_output, fcl::crypto::base64_encode(input));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urlenc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc_tPUB-n5h"s;

   BOOST_CHECK_EQUAL(expected_output, fcl::crypto::base64url_encode(input));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = fcl::crypto::base64_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urldec) try {
   auto input = "YWJjMTIzJCYoKSc_tPUB-n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = fcl::crypto::base64url_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_extraequals) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h========="s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   std::vector<char> b64 = fcl::crypto::base64_decode(input);
   BOOST_CHECK_EQUAL(expected_output, std::string_view(b64.begin(), b64.end()));
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_bad_stuff) try {
   auto input = "YWJjMTIzJCYoKSc/tPU$B+n5h="s;

   BOOST_CHECK_EXCEPTION(fcl::crypto::base64_decode(input), fcl::exceptions::context_error, [](const fcl::exceptions::context_error& e) {
      return std::string(e.what()).find("encountered non-base64 character") != std::string::npos;
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
