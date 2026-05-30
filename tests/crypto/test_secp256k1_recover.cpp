#include <boost/test/unit_test.hpp>
#include <fcl/exception/macros.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

import fcl.exception.exception;
import fcl.crypto.hex;
import fcl.crypto.secp256k1;
import fcl.core.utility;

using namespace fcl;
using namespace fcl::crypto;
using recover_bytes = fcl::crypto::secp256k1::recover_bytes;

#include "test_utils.hpp"

BOOST_AUTO_TEST_SUITE(secp256k1_recover)
BOOST_AUTO_TEST_CASE(recover) try {

   using test_recover = std::tuple<std::string, std::string, recover_bytes>;
   const std::vector<test_recover> tests{
       // test
       {"1b323dd47a1dd5592c296ee2ee12e0af38974087a475e99098a440284f19c1f7642fa0baa10a8a3ab800dfdbe987dee68a09b6fa3db45a"
        "5cc4f3a5835a1671d4dd",
        "92390316873c5a9d520b28aba61e7a8f00025ac069acd9c4d2a71d775a55fa5f",
        to_bytes("044424982f5c4044aaf27444965d15b53f219c8ad332bf98a98a902ebfb05d46cb86ea6fe663aa83fd4ce0a383855dfae9bf7"
                 "a07b779d34c84c347fec79d04c51e")},

   };

   for (const auto& test : tests) {

      const auto& signature = to_bytes(std::get<0>(test));
      const auto& digest = to_bytes(std::get<1>(test));
      const auto& expected_result = std::get<2>(test);

      auto res = fcl::crypto::secp256k1::recover(signature, digest);
      BOOST_CHECK_EQUAL(fcl::crypto::to_hex(res), fcl::crypto::to_hex(expected_result));
   }
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(recover_rejects_invalid_signature) try {
   const auto call_with_bad_recovery_id = [] {
      (void)fcl::crypto::secp256k1::recover(
         to_bytes("01174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c"
                  "074f67be67e631d33aa7"),
         to_bytes("45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399"));
   };

   BOOST_CHECK_EXCEPTION(call_with_bad_recovery_id(), fcl::crypto::secp256k1::exceptions::invalid_signature,
                         [](const fcl::crypto::secp256k1::exceptions::invalid_signature& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.secp256k1"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(recover_rejects_invalid_input_sizes) try {
   const auto call_with_short_signature = [] {
      (void)fcl::crypto::secp256k1::recover(
         to_bytes("174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c07"
                  "4f67be67e631d33aa7"),
         to_bytes("45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399"));
   };
   BOOST_CHECK_EXCEPTION(call_with_short_signature(), fcl::crypto::secp256k1::exceptions::invalid_input,
                         [](const fcl::crypto::secp256k1::exceptions::invalid_input& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.secp256k1"};
   });

   const auto call_with_short_digest = [] {
      (void)fcl::crypto::secp256k1::recover(
         to_bytes("00174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c"
                  "074f67be67e631d33aa7"),
         to_bytes("fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399"));
   };
   BOOST_CHECK_EXCEPTION(call_with_short_digest(), fcl::crypto::secp256k1::exceptions::invalid_input,
                         [](const fcl::crypto::secp256k1::exceptions::invalid_input& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.secp256k1"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
