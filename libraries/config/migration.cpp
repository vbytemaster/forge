module;

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

module fcl.config.migration;

import fcl.config.document;
import fcl.config.value;
import fcl.schema.diagnostic;

namespace fcl::config {
namespace {

[[nodiscard]] schema::diagnostic migration_error(std::string path, std::string code, std::string message) {
   return schema::diagnostic{
      .path = std::move(path),
      .code = std::move(code),
      .level = schema::severity::error,
      .message = std::move(message),
   };
}

[[nodiscard]] std::uint32_t read_version(const document& input, const migration_options& options) {
   const auto* found = input.try_get(options.version_key);
   if (!found) {
      return options.default_version;
   }
   if (const auto* unsigned_value = std::get_if<std::uint64_t>(&found->storage)) {
      if (*unsigned_value > static_cast<std::uint64_t>(UINT32_MAX)) {
         throw std::out_of_range{"config migration version is too large"};
      }
      return static_cast<std::uint32_t>(*unsigned_value);
   }
   if (const auto* signed_value = std::get_if<std::int64_t>(&found->storage)) {
      if (*signed_value < 0 || *signed_value > static_cast<std::int64_t>(UINT32_MAX)) {
         throw std::out_of_range{"config migration version is outside uint32 range"};
      }
      return static_cast<std::uint32_t>(*signed_value);
   }
   throw std::invalid_argument{"config migration version must be an integer"};
}

[[nodiscard]] const migration_step* find_step(const migration_plan& plan, std::uint32_t version) {
   const auto& steps = plan.steps();
   const auto found = std::ranges::find_if(steps, [&](const migration_step& step) {
      return step.from_version == version;
   });
   return found == steps.end() ? nullptr : &*found;
}

} // namespace

migration_plan& migration_plan::step(std::uint32_t from_version, std::uint32_t to_version, std::string description,
                                     std::function<void(document&)> apply) {
   if (to_version <= from_version) {
      throw std::invalid_argument{"config migration step must increase version"};
   }
   if (!apply) {
      throw std::invalid_argument{"config migration step must have an apply function"};
   }
   for (const auto& existing : steps_) {
      if (existing.from_version == from_version) {
         throw std::invalid_argument{"duplicate config migration from-version"};
      }
   }
   steps_.push_back(migration_step{
      .from_version = from_version,
      .to_version = to_version,
      .description = std::move(description),
      .apply = std::move(apply),
   });
   return *this;
}

const std::vector<migration_step>& migration_plan::steps() const noexcept {
   return steps_;
}

std::uint32_t migration_plan::target_version() const noexcept {
   auto target = std::uint32_t{0};
   for (const auto& step : steps_) {
      target = std::max(target, step.to_version);
   }
   return target;
}

bool migration_result::ok() const noexcept {
   return diagnostics.empty();
}

migration_result migrate(document input, const migration_plan& plan, migration_options options) {
   auto result = migration_result{.value = std::move(input)};
   const auto target = plan.target_version();

   try {
      result.from_version = read_version(result.value, options);
   } catch (const std::exception& error) {
      result.diagnostics.push_back(
         migration_error(options.version_key, "config.migration.version", error.what()));
      return result;
   }

   if (result.from_version > target) {
      result.to_version = result.from_version;
      result.diagnostics.push_back(migration_error(
         options.version_key,
         "config.migration.future-version",
         "config version is newer than this binary supports"));
      return result;
   }

   auto current = result.from_version;
   while (current < target) {
      const auto* step = find_step(plan, current);
      if (!step) {
         result.to_version = current;
         result.diagnostics.push_back(migration_error(
            options.version_key,
            "config.migration.missing-step",
            "missing config migration step"));
         return result;
      }
      try {
         step->apply(result.value);
         current = step->to_version;
         result.value.set(options.version_key, current);
      } catch (const std::exception& error) {
         result.to_version = current;
         result.diagnostics.push_back(migration_error(
            options.version_key,
            "config.migration.apply",
            error.what()));
         return result;
      }
   }

   result.to_version = current;
   if (target == 0 && !result.value.try_get(options.version_key)) {
      result.value.set(options.version_key, current);
   }
   return result;
}

} // namespace fcl::config
