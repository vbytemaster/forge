#include <boost/test/unit_test.hpp>
#include <fcl/exception/macros.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

import fcl.exception.exception;
import fcl.crypto.hex;
import fcl.crypto.blake2;
import fcl.core.utility;

using namespace fcl;
using namespace fcl::crypto;

#include "test_utils.hpp"

BOOST_AUTO_TEST_SUITE(blake2)
BOOST_AUTO_TEST_CASE(compress) try {

   struct compress_test {
      std::vector<std::string> params;
      bytes expected;
   };

   const std::vector<compress_test> tests{
       // test1
       {{
            "00000000",
            "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbab"
            "d9831f79217e1319cde05b",
            "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000",
            "0300000000000000",
            "0000000000000000",
            "01",
        },
        to_bytes("08c9bcf367e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d282e6ad7f520e511f6c3e2b8c68059b9442b"
                 "e0454267ce079217e1319cde05b")},

       // test2
       {{
            "0000000c",
            "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbab"
            "d9831f79217e1319cde05b",
            "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000",
            "0300000000000000",
            "0000000000000000",
            "01",
        },
        to_bytes("ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38"
                 "aa8dbf1925ab92386edd4009923")},

       // test3
       {{
            "0000000c",
            "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbab"
            "d9831f79217e1319cde05b",
            "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000",
            "0300000000000000",
            "0000000000000000",
            "00",
        },
        to_bytes("75ab69d3190a562c51aef8d88f1c2775876944407270c42c9844252c26d2875298743e7f6d5ea2f2d3e8d226039cd31b4e426"
                 "ac4f2d3d666a610c2116fde4735")},

       // test4
       {{
            "00000001",
            "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbab"
            "d9831f79217e1319cde05b",
            "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000",
            "0300000000000000",
            "0000000000000000",
            "01",
        },
        to_bytes("b63a380cb2897d521994a85234ee2c181b5f844d2c624c002677e9703449d2fba551b3a8333bcdf5f2f7e08993d53923de3d6"
                 "4fcc68c034e717b9293fed7a421")},
   };

   yield_function_t yield = []() {};

   for (const auto& test : tests) {

      const auto& params = test.params;
      const auto& expected_result = test.expected;

      BOOST_REQUIRE(params.size() == 6);

      uint32_t _rounds = to_uint32(params[0]);
      bytes _h = to_bytes(params[1]);
      bytes _m = to_bytes(params[2]);
      bytes _t0_offset = to_bytes(params[3]);
      bytes _t1_offset = to_bytes(params[4]);
      bool _f = params[5] == "00" ? false : true;

      auto res = blake2b(_rounds, _h, _m, _t0_offset, _t1_offset, _f, yield);

      BOOST_CHECK_EQUAL(fcl::crypto::to_hex(res), fcl::crypto::to_hex(expected_result));
   }
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(compress_rejects_invalid_input_lengths) try {
   const auto call_with_bad_state = [] {
      (void)blake2b(0, to_bytes("00"), bytes(128, 0), bytes(8, 0), bytes(8, 0), true, [] {});
   };

   BOOST_CHECK_EXCEPTION(call_with_bad_state(), fcl::crypto::blake2::exceptions::invalid_input,
                         [](const fcl::crypto::blake2::exceptions::invalid_input& error) {
      return error.code().category().name() == std::string_view{"fcl.crypto.blake2"};
   });
}
FCL_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
