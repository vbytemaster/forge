module;

#include <boost/program_options.hpp>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module fcl.program_options;

import fcl.config.component;
import fcl.config.decode;
import fcl.config.document;
import fcl.config.value;
import fcl.schema;

namespace fcl::program_options {
namespace {

namespace po = boost::program_options;

[[nodiscard]] std::string option_name(const config::component_descriptor& component, const std::string& field) {
   if (component.section.empty()) {
      return field;
   }
   return component.section + "." + field;
}

void add_field_option(po::options_description& description, const std::string& name, schema::value_kind kind,
                      const std::string& text) {
   auto display = text.empty() ? name : text;
   if (kind == schema::value_kind::boolean) {
      description.add_options()(name.c_str(), po::value<std::string>()->implicit_value("true"), display.c_str());
   } else if (kind == schema::value_kind::string_list) {
      description.add_options()(name.c_str(), po::value<std::vector<std::string>>()->composing(), display.c_str());
   } else {
      description.add_options()(name.c_str(), po::value<std::string>(), display.c_str());
   }
}

[[nodiscard]] po::options_description build_description(const config::component_registry& registry,
                                                        std::string caption) {
   auto description = po::options_description{std::move(caption)};
   for (const auto& component : registry.components()) {
      for (const auto& field : component.fields) {
         add_field_option(description, option_name(component, field.name), field.kind, field.description);
         for (const auto& alias : field.aliases) {
            add_field_option(description, option_name(component, alias), field.kind,
                             "alias for " + option_name(component, field.name));
         }
      }
   }
   return description;
}

[[nodiscard]] config::value cli_value(schema::value_kind kind, const std::string& input) {
   switch (kind) {
   case schema::value_kind::boolean: {
      auto parsed = false;
      if (!config::parse_bool_text(input, parsed)) {
         throw std::invalid_argument{"expected boolean value"};
      }
      return parsed;
   }
   case schema::value_kind::signed_integer:
      return static_cast<std::int64_t>(std::stoll(input));
   case schema::value_kind::unsigned_integer:
      return static_cast<std::uint64_t>(std::stoull(input));
   case schema::value_kind::floating:
      return std::stod(input);
   case schema::value_kind::string:
      return input;
   case schema::value_kind::string_list:
      return config::value::array_type{config::value{input}};
   }
   return input;
}

} // namespace

parse_result parse(int argc, const char* const* argv, const config::component_registry& registry) {
   auto result = parse_result{};
   auto description = build_description(registry, "FCL options");
   try {
      auto parsed = po::command_line_parser(argc, argv).options(description).run();
      auto variables = po::variables_map{};
      po::store(parsed, variables);
      po::notify(variables);

      for (const auto& component : registry.components()) {
         for (const auto& field : component.fields) {
            auto names = std::vector<std::string>{field.name};
            names.insert(names.end(), field.aliases.begin(), field.aliases.end());

            for (const auto& name : names) {
               const auto full_name = option_name(component, name);
               if (!variables.count(full_name)) {
                  continue;
               }

               const auto target = option_name(component, field.name);
               try {
                  if (field.kind == schema::value_kind::string_list) {
                     const auto values = variables[full_name].as<std::vector<std::string>>();
                     auto array = config::value::array_type{};
                     array.reserve(values.size());
                     for (const auto& value : values) {
                        array.emplace_back(value);
                     }
                     result.document.set(target, std::move(array));
                  } else {
                     result.document.set(target, cli_value(field.kind, variables[full_name].as<std::string>()));
                  }
               } catch (const std::exception& error) {
                  result.diagnostics.push_back(schema::diagnostic{
                      .path = target,
                      .code = "program_options.convert",
                      .level = schema::severity::error,
                      .message = error.what(),
                  });
               }
               break;
            }
         }
      }
   } catch (const std::exception& error) {
      result.diagnostics.push_back(schema::diagnostic{
          .path = {},
          .code = "program_options.parse",
          .level = schema::severity::error,
          .message = error.what(),
      });
   }
   return result;
}

std::string help(const config::component_registry& registry, std::string caption) {
   auto description = build_description(registry, std::move(caption));
   auto output = std::ostringstream{};
   output << description;
   return output.str();
}

} // namespace fcl::program_options
