module;

#include <string>
#include <vector>

export module fcl.program_options;

import fcl.config.component;
import fcl.config.document;
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::program_options {

struct parse_result {
   config::document document;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return diagnostics.empty();
   }
};

[[nodiscard]] parse_result parse(int argc, const char* const* argv, const config::component_registry& registry);
[[nodiscard]] std::string help(const config::component_registry& registry, std::string caption = "Options");

} // namespace fcl::program_options
