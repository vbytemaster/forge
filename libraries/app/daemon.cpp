module;

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module forge.app.daemon;

import forge.app.application_shell;
import forge.app.runner;
import forge.asio.runtime;
import forge.asio.task_scheduler;
import forge.config.key_path;
import forge.config.value;
import forge.config.document;
import forge.config.component;
import forge.config.decode;
import forge.config.migration;
import forge.env;
import forge.program_options;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;
import forge.yaml;

namespace forge::app {
namespace {

struct parsed_cli {
   forge::config::document document;
   std::vector<std::string> filtered_args;
   std::vector<forge::schema::diagnostic> diagnostics;
   bool help = false;
   bool check_config = false;
   bool print_effective_config = false;
   bool configure = false;
   bool config_explicit = false;
   bool dotenv_explicit = false;

   [[nodiscard]] bool ok() const {
      return diagnostics.empty();
   }
};

const std::vector<forge::program_options::reserved_option>& daemon_reserved_options() {
   static const auto options = std::vector<forge::program_options::reserved_option>{
      {.name = "help", .path = "daemon.help", .kind = forge::schema::value_kind::boolean, .aliases = {"h", "daemon.help"}},
      {.name = "profile", .path = "daemon.profile", .kind = forge::schema::value_kind::string, .aliases = {"daemon.profile"}},
      {.name = "data-dir", .path = "daemon.data-dir", .kind = forge::schema::value_kind::string, .aliases = {"daemon.data-dir"}},
      {.name = "config", .path = "daemon.config", .kind = forge::schema::value_kind::string, .aliases = {"daemon.config"}},
      {.name = "dotenv", .path = "daemon.dotenv", .kind = forge::schema::value_kind::string, .aliases = {"daemon.dotenv"}},
      {.name = "runtime-threads", .path = "daemon.runtime-threads", .kind = forge::schema::value_kind::unsigned_integer, .aliases = {"daemon.runtime-threads"}},
      {.name = "scheduler-queue-depth", .path = "daemon.scheduler-queue-depth", .kind = forge::schema::value_kind::unsigned_integer, .aliases = {"daemon.scheduler-queue-depth"}},
      {.name = "shutdown-timeout-ms", .path = "daemon.shutdown-timeout-ms", .kind = forge::schema::value_kind::unsigned_integer, .aliases = {"daemon.shutdown-timeout-ms"}},
      {.name = "check-config", .path = "daemon.check-config", .kind = forge::schema::value_kind::boolean, .aliases = {"daemon.check-config"}},
      {.name = "print-effective-config", .path = "daemon.print-effective-config", .kind = forge::schema::value_kind::boolean, .aliases = {"daemon.print-effective-config"}},
      {.name = "configure", .path = "daemon.configure", .kind = forge::schema::value_kind::boolean, .aliases = {"daemon.configure"}},
   };
   return options;
}

bool document_bool(const forge::config::document& document, std::string_view path) {
   const auto* value = document.try_get(path);
   if (!value) {
      return false;
   }
   if (const auto* flag = std::get_if<bool>(&value->storage)) {
      return *flag;
   }
   return false;
}

parsed_cli read_daemon_cli(int argc, char** argv) {
   auto pointers = std::vector<const char*>{};
   pointers.reserve(static_cast<std::size_t>(std::max(argc, 0)));
   for (auto index = 0; index < argc; ++index) {
      pointers.push_back(argv == nullptr ? nullptr : argv[index]);
   }

   auto scanned = forge::program_options::pre_scan_reserved(static_cast<int>(pointers.size()), pointers.data(),
                                                          daemon_reserved_options());
   auto parsed = parsed_cli{};
   parsed.document = std::move(scanned.document);
   parsed.filtered_args = std::move(scanned.filtered_args);
   parsed.diagnostics = std::move(scanned.diagnostics);
   parsed.help = document_bool(parsed.document, "daemon.help");
   parsed.check_config = document_bool(parsed.document, "daemon.check-config");
   parsed.print_effective_config = document_bool(parsed.document, "daemon.print-effective-config");
   parsed.configure = document_bool(parsed.document, "daemon.configure");
   parsed.config_explicit = scanned.present("daemon.config");
   parsed.dotenv_explicit = scanned.present("daemon.dotenv");
   if (parsed.filtered_args.empty()) {
      parsed.filtered_args.push_back("forge-daemon");
   }
   return parsed;
}

std::filesystem::path default_data_dir(const daemon_options& options) {
   const auto name = options.default_data_dir_name.empty() ? options.name : options.default_data_dir_name;
   if (const auto home = forge::env::home_directory()) {
      return *home / ".forge" / name;
   }
   return std::filesystem::temp_directory_path() / "forge" / name;
}

forge::config::component_descriptor daemon_descriptor() {
   return forge::config::component_descriptor{
      .section = "daemon",
      .fields =
         {
            forge::config::field_descriptor{
               .name = "profile",
               .kind = forge::schema::value_kind::string,
               .has_default = true,
               .default_value = "dev_local",
               .description = "daemon profile name",
            },
            forge::config::field_descriptor{
               .name = "data-dir",
               .kind = forge::schema::value_kind::string,
               .description = "daemon data directory",
            },
            forge::config::field_descriptor{
               .name = "config",
               .kind = forge::schema::value_kind::string,
               .description = "daemon YAML config path",
            },
            forge::config::field_descriptor{
               .name = "dotenv",
               .kind = forge::schema::value_kind::string,
               .description = "daemon .env config path",
            },
            forge::config::field_descriptor{
               .name = "runtime-threads",
               .kind = forge::schema::value_kind::unsigned_integer,
               .has_default = true,
               .default_value = 1U,
               .description = "runtime worker thread count",
            },
            forge::config::field_descriptor{
               .name = "scheduler-queue-depth",
               .kind = forge::schema::value_kind::unsigned_integer,
               .has_default = true,
               .default_value = 4096U,
               .description = "maximum pending scheduler tasks",
            },
            forge::config::field_descriptor{
               .name = "shutdown-timeout-ms",
               .kind = forge::schema::value_kind::unsigned_integer,
               .has_default = true,
               .default_value = 10'000U,
               .description = "shutdown timeout in milliseconds",
            },
            forge::config::field_descriptor{
               .name = "help",
               .kind = forge::schema::value_kind::boolean,
               .has_default = true,
               .default_value = false,
               .description = "show daemon and application options",
            },
            forge::config::field_descriptor{
               .name = "check-config",
               .kind = forge::schema::value_kind::boolean,
               .has_default = true,
               .default_value = false,
               .description = "validate config and exit",
            },
            forge::config::field_descriptor{
               .name = "print-effective-config",
               .kind = forge::schema::value_kind::boolean,
               .has_default = true,
               .default_value = false,
               .description = "print redacted effective config and exit",
            },
            forge::config::field_descriptor{
               .name = "configure",
               .kind = forge::schema::value_kind::boolean,
               .has_default = true,
               .default_value = false,
               .description = "write example config and exit",
            },
         },
   };
}

forge::config::component_registry daemon_registry() {
   auto registry = forge::config::component_registry{};
   registry.add(daemon_descriptor());
   return registry;
}

forge::config::component_registry full_registry(const forge::config::component_registry& app_registry) {
   auto registry = daemon_registry();
   for (auto component : app_registry.components()) {
      registry.add(std::move(component));
   }
   return registry;
}

std::size_t blocking_budget(unsigned runtime_threads) {
   if (runtime_threads <= 1) {
      return 1;
   }
   return static_cast<std::size_t>(runtime_threads - 1);
}

forge::config::document dynamic_daemon_defaults(const daemon_options& options) {
   auto output = forge::config::document{};
   const auto data_dir = default_data_dir(options);
   output.set("daemon.profile", options.default_profile);
   output.set("daemon.data-dir", data_dir.string());
   output.set("daemon.runtime-threads", 1U);
   output.set("daemon.scheduler-queue-depth", 4096U);
   output.set("daemon.shutdown-timeout-ms", 10'000U);
   output.set("daemon.help", false);
   output.set("daemon.check-config", false);
   output.set("daemon.print-effective-config", false);
   output.set("daemon.configure", false);
   return output;
}

std::string string_field(const forge::config::document& document, std::string_view path, std::string fallback = {}) {
   const auto* value = document.try_get(path);
   if (!value) {
      return fallback;
   }
   if (const auto* text = std::get_if<std::string>(&value->storage)) {
      return *text;
   }
   throw std::invalid_argument{"daemon config value must be a string: " + std::string{path}};
}

std::uint64_t unsigned_field(const forge::config::document& document, std::string_view path, std::uint64_t fallback) {
   const auto* value = document.try_get(path);
   if (!value) {
      return fallback;
   }
   if (const auto* number = std::get_if<std::uint64_t>(&value->storage)) {
      return *number;
   }
   if (const auto* signed_number = std::get_if<std::int64_t>(&value->storage); signed_number && *signed_number >= 0) {
      return static_cast<std::uint64_t>(*signed_number);
   }
   throw std::invalid_argument{"daemon config value must be an unsigned integer: " + std::string{path}};
}

bool bool_field(const forge::config::document& document, std::string_view path, bool fallback) {
   const auto* value = document.try_get(path);
   if (!value) {
      return fallback;
   }
   if (const auto* flag = std::get_if<bool>(&value->storage)) {
      return *flag;
   }
   if (const auto* text = std::get_if<std::string>(&value->storage)) {
      auto parsed = false;
      if (forge::config::parse_bool_text(*text, parsed)) {
         return parsed;
      }
   }
   throw std::invalid_argument{"daemon config value must be a boolean: " + std::string{path}};
}

std::vector<const char*> argv_view(const std::vector<std::string>& arguments) {
   auto output = std::vector<const char*>{};
   output.reserve(arguments.size());
   for (const auto& argument : arguments) {
      output.push_back(argument.c_str());
   }
   return output;
}

void append_diagnostics(std::vector<forge::schema::diagnostic>& target,
                        const std::vector<forge::schema::diagnostic>& source) {
   target.insert(target.end(), source.begin(), source.end());
}

void print_diagnostics(const std::vector<forge::schema::diagnostic>& diagnostics) {
   for (const auto& entry : diagnostics) {
      const auto severity = entry.level == forge::schema::severity::error ? "error" : "warning";
      std::cerr << severity << ": " << entry.code;
      if (!entry.path.empty()) {
         std::cerr << " at " << entry.path;
      }
      if (!entry.message.empty()) {
         std::cerr << ": " << entry.message;
      }
      std::cerr << '\n';
   }
}

int fail_with_diagnostics(const std::vector<forge::schema::diagnostic>& diagnostics) {
   print_diagnostics(diagnostics);
   return 1;
}

bool has_errors(const std::vector<forge::schema::diagnostic>& diagnostics) {
   for (const auto& entry : diagnostics) {
      if (entry.level == forge::schema::severity::error) {
         return true;
      }
   }
   return false;
}

void append_bootstrap_diagnostic(std::vector<forge::schema::diagnostic>& diagnostics, const std::exception& error) {
   diagnostics.push_back(forge::schema::diagnostic{
      .path = "daemon",
      .code = "daemon.bootstrap",
      .level = forge::schema::severity::error,
      .message = error.what(),
   });
}

int fail_with_bootstrap_diagnostic(std::vector<forge::schema::diagnostic>& diagnostics, const std::exception& error) {
   append_bootstrap_diagnostic(diagnostics, error);
   return fail_with_diagnostics(diagnostics);
}

forge::config::document read_yaml_layer(const std::filesystem::path& path, bool required,
                                      std::vector<forge::schema::diagnostic>& diagnostics) {
   if (path.empty() || !std::filesystem::exists(path)) {
      if (required) {
         diagnostics.push_back(forge::schema::diagnostic{
             .path = "daemon.config",
             .code = "daemon.config_missing",
             .level = forge::schema::severity::error,
             .message = "config file does not exist: " + path.string(),
         });
      }
      return {};
   }
   auto parsed = forge::yaml::load_document(path, forge::yaml::read_options{.source_name = path.string()});
   append_diagnostics(diagnostics, parsed.diagnostics);
   return parsed.ok() ? std::move(parsed.value) : forge::config::document{};
}

forge::config::document read_dotenv_layer(const std::filesystem::path& path,
                                        const forge::config::component_registry& registry,
                                        const daemon_options& options, bool required,
                                        std::vector<forge::schema::diagnostic>& diagnostics) {
   if (!options.read_dotenv || options.env_prefix.empty()) {
      return {};
   }
   if (path.empty() || !std::filesystem::exists(path)) {
      if (required) {
         diagnostics.push_back(forge::schema::diagnostic{
            .path = "daemon.dotenv",
            .code = "daemon.dotenv_missing",
            .level = forge::schema::severity::error,
            .message = ".env file does not exist: " + path.string(),
         });
      }
      return {};
   }
   auto parsed = forge::env::load_document(
      path,
      registry,
      forge::env::read_options{
         .prefix = options.env_prefix,
         .source_name = path.string(),
      });
   append_diagnostics(diagnostics, parsed.diagnostics);
   return parsed.ok() ? std::move(parsed.value) : forge::config::document{};
}

forge::config::document read_process_env_layer(const forge::config::component_registry& registry,
                                             const daemon_options& options,
                                             forge::env::unknown_variable_policy unknown_variables,
                                             std::vector<forge::schema::diagnostic>& diagnostics) {
   if (!options.read_process_env || options.env_prefix.empty()) {
      return {};
   }
   auto parsed = forge::env::read_process_document(registry, forge::env::read_options{
                                                              .prefix = options.env_prefix,
                                                              .unknown_variables = unknown_variables,
                                                           });
   append_diagnostics(diagnostics, parsed.diagnostics);
   return parsed.ok() ? std::move(parsed.value) : forge::config::document{};
}

forge::config::document read_product_cli_layer(const std::vector<std::string>& filtered_args,
                                             const forge::config::component_registry& registry,
                                             const daemon_options& options,
                                             std::vector<forge::schema::diagnostic>& diagnostics) {
   if (!options.read_cli) {
      return {};
   }
   const auto pointers = argv_view(filtered_args);
   auto parsed = forge::program_options::parse(static_cast<int>(pointers.size()), pointers.data(), registry);
   append_diagnostics(diagnostics, parsed.diagnostics);
   return parsed.ok() ? std::move(parsed.document) : forge::config::document{};
}

daemon_context context_from_document(const daemon_options& options, const forge::config::document& document) {
   auto context = daemon_context{};
   const auto config_filename = options.config_filename.empty() ? std::filesystem::path{"config.yml"}
                                                                : std::filesystem::path{options.config_filename};
   const auto dotenv_filename = options.dotenv_filename.empty() ? std::filesystem::path{".env"}
                                                                : std::filesystem::path{options.dotenv_filename};
   context.name = options.name.empty() ? "forge-daemon" : options.name;
   context.profile = string_field(document, "daemon.profile", options.default_profile);
   context.data_dir = string_field(document, "daemon.data-dir", default_data_dir(options).string());
   context.config_path = string_field(document, "daemon.config", (context.data_dir / config_filename).string());
   context.dotenv_path = string_field(document, "daemon.dotenv", (context.data_dir / dotenv_filename).string());

   const auto runtime_threads = unsigned_field(document, "daemon.runtime-threads", 1);
   const auto queue_depth = unsigned_field(document, "daemon.scheduler-queue-depth", 4096);
   context.shell = forge::app::application_shell_options{
      .name = context.name,
      .runtime =
         forge::asio::runtime_options{
            .worker_threads = static_cast<std::size_t>(runtime_threads),
            .thread_name = context.name,
         },
      .scheduler =
         forge::asio::task_scheduler::options{
            .max_blocking_tasks = blocking_budget(runtime_threads),
            .max_pending_tasks = static_cast<std::size_t>(queue_depth),
         },
   };
   return context;
}

forge::config::document resolve_daemon_paths(forge::config::document document, const daemon_options& options) {
   const auto context = context_from_document(options, document);
   if (!document.try_get("daemon.data-dir")) {
      document.set("daemon.data-dir", context.data_dir.string());
   }
   if (!document.try_get("daemon.config")) {
      document.set("daemon.config", context.config_path.string());
   }
   if (!document.try_get("daemon.dotenv")) {
      document.set("daemon.dotenv", context.dotenv_path.string());
   }
   return document;
}

void reset_daemon_action_flags(forge::config::document& document) {
   document.set("daemon.help", false);
   document.set("daemon.check-config", false);
   document.set("daemon.print-effective-config", false);
   document.set("daemon.configure", false);
}

void apply_run_options_from_document(forge::app::run_options& options, const forge::config::document& document) {
   const auto timeout_ms = unsigned_field(document, "daemon.shutdown-timeout-ms", 10'000);
   const auto clamped = timeout_ms > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                           ? std::numeric_limits<int>::max()
                           : static_cast<int>(timeout_ms);
   options.shutdown_timeout = std::chrono::milliseconds{clamped};
}

std::string daemon_help(const daemon_options& options, const forge::config::component_registry& registry) {
   auto output = std::string{};
   output += options.display_name.empty() ? options.name : options.display_name;
   output += "\n\nDaemon options:\n";
   output += "  --help, -h\n";
   output += "  --profile <name>\n";
   output += "  --data-dir <path>\n";
   output += "  --config <path>\n";
   output += "  --dotenv <path>\n";
   output += "  --runtime-threads <count>\n";
   output += "  --scheduler-queue-depth <count>\n";
   output += "  --shutdown-timeout-ms <milliseconds>\n";
   output += "  --check-config\n";
   output += "  --print-effective-config\n";
   output += "  --configure\n\n";
   output += forge::program_options::help(registry, "Application and plugin options");
   return output;
}

} // namespace

int run_daemon(daemon_factory make_app, int argc, char** argv, daemon_options options) {
   if (!make_app) {
      std::cerr << "error: daemon factory is empty\n";
      return 1;
   }

   auto diagnostics = std::vector<forge::schema::diagnostic>{};
   auto daemon_cli = read_daemon_cli(argc, argv);
   append_diagnostics(diagnostics, daemon_cli.diagnostics);
   if (has_errors(diagnostics)) {
      return fail_with_diagnostics(diagnostics);
   }

   const auto daemon_defaults = dynamic_daemon_defaults(options);
   const auto daemon_only_registry = daemon_registry();

   if (daemon_cli.help) {
      try {
         const auto context = context_from_document(options, forge::config::merge({daemon_defaults, daemon_cli.document}));
         auto app = make_app(context);
         if (!app) {
            std::cerr << "error: daemon factory returned null application\n";
            return 1;
         }
         std::cout << daemon_help(options, app->describe_config());
         return 0;
      } catch (const std::exception& error) {
         return fail_with_bootstrap_diagnostic(diagnostics, error);
      }
   }

   try {
      auto early_process_env =
         read_process_env_layer(daemon_only_registry, options, forge::env::unknown_variable_policy::ignore, diagnostics);
      if (has_errors(diagnostics)) {
         return fail_with_diagnostics(diagnostics);
      }

      auto early_without_yaml = forge::config::merge({daemon_defaults, early_process_env, daemon_cli.document});
      auto early_context = context_from_document(options, early_without_yaml);
      auto yaml = options.read_yaml ? read_yaml_layer(
                                         early_context.config_path,
                                         daemon_cli.config_explicit && !daemon_cli.configure && !daemon_cli.help,
                                         diagnostics)
                                    : forge::config::document{};
      if (has_errors(diagnostics)) {
         return fail_with_diagnostics(diagnostics);
      }

      auto early_paths = resolve_daemon_paths(
         forge::config::merge({daemon_defaults, yaml, early_process_env, daemon_cli.document}), options);
      auto early_path_context = context_from_document(options, early_paths);
      auto early_dotenv = read_dotenv_layer(
         early_path_context.dotenv_path,
         daemon_only_registry,
         options,
         daemon_cli.dotenv_explicit && !daemon_cli.configure && !daemon_cli.help,
         diagnostics);
      if (has_errors(diagnostics)) {
         return fail_with_diagnostics(diagnostics);
      }

      auto early_effective = resolve_daemon_paths(
         forge::config::merge({daemon_defaults, yaml, early_dotenv, early_process_env, daemon_cli.document}), options);
      auto context = context_from_document(options, early_effective);
      auto app = make_app(context);
      if (!app) {
         std::cerr << "error: daemon factory returned null application\n";
         return 1;
      }

      const auto app_registry = app->describe_config();
      auto registry = full_registry(app_registry);

      auto dotenv = read_dotenv_layer(
         context.dotenv_path,
         registry,
         options,
         daemon_cli.dotenv_explicit && !daemon_cli.configure && !daemon_cli.help,
         diagnostics);
      auto process_env =
         read_process_env_layer(registry, options, forge::env::unknown_variable_policy::warn, diagnostics);
      auto product_cli = read_product_cli_layer(daemon_cli.filtered_args, app_registry, options, diagnostics);
      if (has_errors(diagnostics)) {
         return fail_with_diagnostics(diagnostics);
      }

      auto effective = resolve_daemon_paths(
         forge::config::merge({
            forge::config::defaults_for(registry),
            daemon_defaults,
            yaml,
            dotenv,
            process_env,
            daemon_cli.document,
            product_cli,
         }),
         options);

      const auto help = bool_field(effective, "daemon.help", false);
      const auto check_config = bool_field(effective, "daemon.check-config", false);
      const auto print_effective_config = bool_field(effective, "daemon.print-effective-config", false);
      const auto configure = bool_field(effective, "daemon.configure", false);

      if (help) {
         std::cout << daemon_help(options, app_registry);
         return 0;
      }
      if (configure) {
         if (!options.allow_configure) {
            std::cerr << "error: --configure is disabled for this daemon\n";
            return 1;
         }
         const auto config_path = string_field(effective, "daemon.config", context.config_path.string());
         if (std::filesystem::exists(config_path)) {
            std::cerr << "error: refusing to overwrite existing config: " << config_path << '\n';
            return 1;
         }
         const auto parent = std::filesystem::path{config_path}.parent_path();
         if (!parent.empty()) {
            std::filesystem::create_directories(parent);
         }
         auto generated =
            forge::config::merge({forge::config::defaults_for(registry), daemon_defaults, early_process_env,
                                daemon_cli.document});
         reset_daemon_action_flags(generated);
         generated = resolve_daemon_paths(std::move(generated), options);
         auto saved = forge::yaml::save_document(config_path, forge::config::redact(std::move(generated), registry));
         append_diagnostics(diagnostics, saved.diagnostics);
         if (!saved.ok()) {
            return fail_with_diagnostics(diagnostics);
         }
         return 0;
      }
      if (print_effective_config) {
         if (!options.allow_print_effective_config) {
            std::cerr << "error: --print-effective-config is disabled for this daemon\n";
            return 1;
         }
         auto written = forge::yaml::write_document(forge::config::redact(effective, registry));
         append_diagnostics(diagnostics, written.diagnostics);
         if (!written.ok()) {
            return fail_with_diagnostics(diagnostics);
         }
         std::cout << written.text;
         return 0;
      }
      if (check_config) {
         if (!options.allow_check_config) {
            std::cerr << "error: --check-config is disabled for this daemon\n";
            return 1;
         }
         try {
            app->configure(effective);
         } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
         }
         return 0;
      }

      auto run_options = options.run;
      apply_run_options_from_document(run_options, effective);
      try {
         return forge::app::run_application(std::move(app), effective, std::move(run_options));
      } catch (const std::exception& error) {
         std::cerr << "error: " << error.what() << '\n';
         return 1;
      }
   } catch (const std::exception& error) {
      return fail_with_bootstrap_diagnostic(diagnostics, error);
   }
}

} // namespace forge::app
