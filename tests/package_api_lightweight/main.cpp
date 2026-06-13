#include <utility>

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.registry;
import fcl.api.binding;

int main() {
   auto registry = fcl::api::registry{};
   const auto plan = std::move(fcl::api::binding().serve(registry)).build();
   const auto available = fcl::api::descriptor{
       .id = {.value = "package.smoke"},
       .version = {.major = 1, .revision = 2},
   };
   const auto requested = fcl::api::api_ref{
       .id = {.value = "package.smoke"},
       .major = 1,
       .min_revision = 2,
   };
   return fcl::api::compatible(available, requested) && plan.local == &registry ? 0 : 1;
}
