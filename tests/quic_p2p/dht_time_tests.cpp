module;

#include <boost/test/unit_test.hpp>

#include <chrono>

#include "../../libraries/net/p2p/details/rendezvous_time.hxx"

module forge.net.p2p.node;

#include "../../libraries/net/p2p/details/dht_time.hxx"

namespace forge::net::p2p {

BOOST_AUTO_TEST_SUITE(dht_time_tests)

BOOST_AUTO_TEST_CASE(dht_expiry_saturates_at_system_clock_maximum) {
   const auto maximum = (std::chrono::system_clock::time_point::max)();
   const auto near_maximum = maximum - std::chrono::seconds{5};

   BOOST_CHECK(detail::dht_expiry_after(near_maximum, std::chrono::seconds{10}) == maximum);
   BOOST_CHECK(detail::dht_expiry_after(near_maximum, std::chrono::seconds{3}) ==
               near_maximum + std::chrono::seconds{3});
   BOOST_CHECK(detail::dht_expiry_after(near_maximum, std::chrono::seconds::zero()) == near_maximum);
}

BOOST_AUTO_TEST_CASE(rendezvous_expiry_saturates_at_system_clock_maximum) {
   const auto maximum = (std::chrono::system_clock::time_point::max)();
   const auto near_maximum = maximum - std::chrono::seconds{5};

   BOOST_CHECK(detail::rendezvous_expiry_after(near_maximum, std::chrono::seconds::max()) == maximum);
   BOOST_CHECK(detail::rendezvous_expiry_after(near_maximum, std::chrono::seconds{3}) ==
               near_maximum + std::chrono::seconds{3});
   BOOST_CHECK(detail::rendezvous_expiry_after(near_maximum, std::chrono::seconds::zero()) == near_maximum);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
