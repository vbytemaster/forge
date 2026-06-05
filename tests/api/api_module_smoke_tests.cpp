#include <boost/test/unit_test.hpp>

#include <memory>
#include <utility>

import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.registry;
import fcl.api.binding;

BOOST_AUTO_TEST_SUITE(api_module_smoke_suite)

BOOST_AUTO_TEST_CASE(leaf_modules_can_be_imported_without_aggregate) {
   const auto available = fcl::api::descriptor{
       .id = {.value = "module.smoke"},
       .version = {.major = 1, .revision = 4},
   };
   const auto requested = fcl::api::api_ref{
       .id = {.value = "module.smoke"},
       .major = 1,
       .min_revision = 3,
   };

   auto registry = fcl::api::registry{};
   auto installer = fcl::api::installer{registry};
   auto view = fcl::api::view{registry};
   auto plan = std::move(fcl::api::binding().serve(registry)).build();
   const auto frame = fcl::api::frame{
       .kind = fcl::api::frame_kind::cancel,
       .api = requested,
       .codec = {.value = "fcl.raw"},
   };

   static_cast<void>(installer);
   BOOST_TEST(fcl::api::compatible(available, requested));
   BOOST_TEST(plan.local == &registry);
   BOOST_TEST(&view.registry_ref() == &registry);
   BOOST_CHECK(frame.kind == fcl::api::frame_kind::cancel);
}

BOOST_AUTO_TEST_SUITE_END()
