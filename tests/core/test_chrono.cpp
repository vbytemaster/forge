#include <boost/test/unit_test.hpp>
#include <chrono>
#include <stdexcept>

import forge.core.chrono;

BOOST_AUTO_TEST_SUITE(core_chrono_test_suite)

BOOST_AUTO_TEST_CASE(chrono_iso_roundtrip) {
   const std::chrono::sys_seconds one_second{std::chrono::seconds{1}};
   BOOST_CHECK_EQUAL(forge::chrono::to_iso_string(one_second), "1970-01-01T00:00:01");
   BOOST_CHECK_EQUAL(forge::chrono::to_non_delimited_iso_string(one_second), "19700101T000001");
   BOOST_CHECK(forge::chrono::from_iso_seconds("1970-01-01T00:00:01") == one_second);
   BOOST_CHECK(forge::chrono::from_iso_seconds("19700101T000001") == one_second);

   const std::chrono::sys_time<std::chrono::microseconds> one_second_us{std::chrono::seconds{1}};
   BOOST_CHECK_EQUAL(forge::chrono::to_iso_string(one_second_us), "1970-01-01T00:00:01.000");
   BOOST_CHECK(forge::chrono::from_iso_time_point("1970-01-01T00:00:01.000") == one_second_us);
}

BOOST_AUTO_TEST_CASE(chrono_invalid_iso_fails) {
   BOOST_CHECK_THROW(forge::chrono::from_iso_seconds("not-a-time"), std::invalid_argument);
   BOOST_CHECK_THROW(forge::chrono::from_iso_time_point("not-a-time"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(chrono_wire_helpers_preserve_fc_layout_values) {
   const std::chrono::sys_time<std::chrono::microseconds> one_second_us{std::chrono::seconds{1}};
   BOOST_CHECK_EQUAL(forge::chrono::to_fc_time_point_wire(one_second_us), 1'000'000u);
   BOOST_CHECK(forge::chrono::from_fc_time_point_wire(1'000'000u) == one_second_us);

   const std::chrono::sys_seconds one_second{std::chrono::seconds{1}};
   BOOST_CHECK_EQUAL(forge::chrono::to_fc_time_point_sec_wire(one_second), 1u);
   BOOST_CHECK(forge::chrono::from_fc_time_point_sec_wire(1u) == one_second);

   BOOST_CHECK_EQUAL(forge::chrono::to_fc_microseconds_wire(std::chrono::microseconds{-1}), 0xffffffffffffffffULL);
   BOOST_CHECK(forge::chrono::from_fc_microseconds_wire(0xffffffffffffffffULL) == std::chrono::microseconds{-1});
}

BOOST_AUTO_TEST_SUITE_END()
