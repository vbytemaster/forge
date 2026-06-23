#include <boost/test/unit_test.hpp>

#include <memory>
#include <utility>

import forge.api.types;
import forge.api.descriptor;
import forge.api.registry;
import forge.api.binding;

BOOST_AUTO_TEST_SUITE(api_module_smoke_suite)

BOOST_AUTO_TEST_CASE(leaf_modules_can_be_imported_without_aggregate) {
   const auto available = forge::api::descriptor{
       .id = {.value = "module.smoke"},
       .version = {.major = 1, .revision = 4},
   };
   const auto requested = forge::api::api_ref{
       .id = {.value = "module.smoke"},
       .major = 1,
       .min_revision = 3,
   };

   auto registry = forge::api::registry{};
   auto installer = forge::api::installer{registry};
   auto view = forge::api::view{registry};
   auto plan = std::move(forge::api::binding().serve(registry)).build();
   const auto frame = forge::api::frame{
       .kind = forge::api::frame_kind::cancel,
       .api = requested,
       .codec = {.value = "forge.raw"},
   };

   static_cast<void>(installer);
   BOOST_TEST(forge::api::compatible(available, requested));
   BOOST_TEST(plan.local == &registry);
   BOOST_TEST(&view.registry_ref() == &registry);
   BOOST_CHECK(frame.kind == forge::api::frame_kind::cancel);
}

BOOST_AUTO_TEST_SUITE_END()
