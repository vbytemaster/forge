#include <boost/test/unit_test.hpp>
#include <string>

import fcl.variant.dynamic_bitset;
import fcl.variant.variant_dynamic_bitset;
import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;
import fcl.exceptions;

using namespace fcl;
using std::string;

BOOST_AUTO_TEST_SUITE(dynamic_bitset_test_suite)

BOOST_AUTO_TEST_CASE(dynamic_bitset_test) {
   constexpr uint8_t bits = 0b0000000001010100;
   fcl::dynamic_bitset bs(16, bits); // 2 blocks of uint8_t

   fcl::mutable_variant_object mu;
   mu("bs", bs);

   fcl::dynamic_bitset bs2;
   fcl::from_variant(mu["bs"], bs2);

   BOOST_TEST(bs2 == bs);
}

BOOST_AUTO_TEST_SUITE_END()
