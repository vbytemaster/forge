#include <utility>

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.registry;
import forge.api.binding;

int main() {
   auto registry = forge::api::registry{};
   const auto plan = std::move(forge::api::binding().serve(registry)).build();
   const auto available = forge::api::descriptor{
       .id = {.value = "package.smoke"},
       .version = {.major = 1, .revision = 2},
   };
   const auto requested = forge::api::api_ref{
       .id = {.value = "package.smoke"},
       .major = 1,
       .min_revision = 2,
   };
   return forge::api::compatible(available, requested) && plan.local == &registry ? 0 : 1;
}
