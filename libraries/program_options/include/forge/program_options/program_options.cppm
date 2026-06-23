module;

#include <string>
#include <string_view>
#include <vector>

export module forge.program_options;

import forge.config.component;
import forge.config.document;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

export namespace forge::program_options {

struct parse_result {
   config::document document;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const {
      return diagnostics.empty();
   }
};

struct reserved_option {
   std::string name;
   std::string path;
   schema::value_kind kind = schema::value_kind::string;
   std::vector<std::string> aliases;
};

struct pre_scan_result {
   config::document document;
   std::vector<std::string> filtered_args;
   std::vector<schema::diagnostic> diagnostics;
   std::vector<std::string> present_paths;

   [[nodiscard]] bool ok() const {
      return diagnostics.empty();
   }

   [[nodiscard]] bool present(std::string_view path) const;
};

[[nodiscard]] parse_result parse(int argc, const char* const* argv, const config::component_registry& registry);
[[nodiscard]] pre_scan_result pre_scan_reserved(int argc, const char* const* argv,
                                                const std::vector<reserved_option>& reserved);
[[nodiscard]] std::string help(const config::component_registry& registry, std::string caption = "Options");

} // namespace forge::program_options
