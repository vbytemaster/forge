#include <boost/test/unit_test.hpp>
#include <string>

import forge.variant.dynamic_bitset;
import forge.variant.variant_dynamic_bitset;
import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.exceptions;

using namespace forge;
using std::string;

BOOST_AUTO_TEST_SUITE(dynamic_bitset_test_suite)

BOOST_AUTO_TEST_CASE(dynamic_bitset_test) {
   constexpr uint8_t bits = 0b0000000001010100;
   forge::dynamic_bitset bs(16, bits); // 2 blocks of uint8_t

   forge::mutable_variant_object mu;
   mu("bs", bs);

   forge::dynamic_bitset bs2;
   forge::from_variant(mu["bs"], bs2);

   BOOST_TEST(bs2 == bs);
}

BOOST_AUTO_TEST_SUITE_END()
