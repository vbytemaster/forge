# fcl_app

`fcl_app` is the opinionated application shell for FCL services. It owns the
runtime objects that every daemon tends to duplicate by hand: `runtime`,
`task_scheduler`, API registry, signal bus, event bus, diagnostics, plugin
registry, plugin context and lifecycle runtime.

The preferred production entrypoint is `fcl::app::application_builder`, which
returns an `application_shell` without asking the application to subclass the shell.
Derived `application_shell` classes remain available as an advanced escape hatch
for applications with substantial state or custom lifecycle hooks.

## When To Use

- A program has plugins with config, dependency order and async lifecycle.
- A daemon needs one place to collect app config and plugin config.
- A foreground daemon wants standard YAML, `.env`, process env, CLI and default
  merge before lifecycle starts.
- You want deterministic startup, rollback, reverse shutdown and diagnostics.
- You want typed APIs between plugins without coupling them through globals.
- Plugins must publish config descriptors without knowing whether values came
  from source adapters such as YAML, JSON, `.env`, process env or CLI.

## When Not To Use

- Do not use `fcl_app` as a generic dependency injection container.
- Do not parse `argv`, YAML or JSON inside plugins. Use `run_daemon(...)` for
  normal foreground daemons, or use `fcl_program_options`, `fcl_yaml` and
  `fcl_json` before `application_shell::configure(...)` in custom hosts.
- Do not put security authority into UI/events/diagnostics. They are
  observability surfaces, not permission boundaries.
- Do not invent hook names that repeat the application context. The shell
  context already says this is application code.

## Public Modules

- `fcl.app.application_shell` — production app shell and hook contexts.
- `fcl.app.application_builder` — convenience builder that creates an
  `application_shell`.
- `fcl.app.daemon` — foreground daemon runner that loads YAML, explicit `.env`,
  process env and CLI, merges defaults and handles help/check/print/configure
  actions.
- `fcl.app.runner` — foreground lifecycle runner with signal policy.
- `fcl.app.application` — lower-level `application_base` and
  `application_runtime`.
- `fcl.app.plugin`, `fcl.app.plugin_context`, `fcl.app.plugin_registry`.
- `fcl.app.events`, `fcl.app.diagnostics`, `fcl.app.signals`.
- `fcl.app` — aggregate import.

Target: `fcl_app`.

Dependencies: `fcl_asio`, `fcl_config`, `fcl_yaml`, `fcl_env`,
`fcl_program_options`, Boost headers.

## Examples

The examples below show the intended production shape: app-owned config stays
in the shell, plugin-owned config stays in plugins, and lifecycle calls remain
shell-owned.

## Production Shape

`application_shell` makes lifecycle methods non-overridable:

```cpp
boost::asio::awaitable<void> run_configured_app(
   fcl::app::application_shell& app,
   const fcl::config::document& document) {
   app.describe_config();
   app.configure(document);
   co_await app.initialize();
   co_await app.startup();
   app.request_stop();
   co_await app.shutdown();
}
```

Derived applications only implement hooks:

- `on_describe_config(registry&)`
- `on_configure(configure_context&)`
- `on_register_plugins(plugin_registry&)`
- `on_provide(application_context&)`
- `on_run_foreground()`

This is deliberately strict. The application controls composition, but FCL controls
the order: collect config, configure app and plugins, provide app APIs, let
plugins provide APIs, initialize
plugins, startup plugins, request stop, shutdown in reverse order.

## App And Plugin Config

A plugin owns its own config. The application owns only app-level config. The
shell merges defaults from every registered descriptor with the input document
before calling `on_configure(...)` and plugin `configure(...)`.

```cpp
#include <boost/describe.hpp>

#include <cstdint>

import fcl.app;
import fcl.config;
import fcl.schema;

struct http_config {
   std::uint16_t bind_port = 8080;
   bool tls_enabled = false;
};

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, tls_enabled))

template <>
struct fcl::schema::rules<http_config> {
   static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port").default_value(8080).range(1, 65'535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      return schema;
   }
};

class http_plugin final : public fcl::app::plugin {
 public:
   fcl::app::plugin_id id() const override { return fcl::app::plugin_id{"http"}; }
   std::string version() const override { return "1"; }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::describe_component<http_config>("http");
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      bind_port_ = view.get_or<std::uint16_t>("bind-port", 8080);
      tls_enabled_ = view.get_or<bool>("tls-enabled", false);
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      context.events().publish(fcl::app::event_severity::info, "http.initialize", "configured");
      co_return;
   }

   boost::asio::awaitable<void> startup() override { co_return; }
   boost::asio::awaitable<void> shutdown() override { co_return; }

 private:
   std::uint16_t bind_port_ = 8080;
   bool tls_enabled_ = false;
};
```

## Plugin Enable/Disable

Every registered plugin gets a shell-owned selection key:

```yaml
plugins:
   http:
      enabled: true
   metrics:
      enabled: false
```

The default comes from `plugin_descriptor.enabled_by_default`. Disabled plugins
are not configured, initialized, started or shut down. If an enabled plugin
depends on a disabled plugin, the shell fails before lifecycle side effects.

```cpp
void on_register_plugins(fcl::app::plugin_registry& registry) override {
   registry.register_plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{"store"},
      .factory = [] { return std::make_unique<store_plugin>(); },
   });
   registry.register_plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{"api"},
      .dependencies = {fcl::app::plugin_id{"store"}},
      .factory = [] { return std::make_unique<api_plugin>(); },
   });
   registry.register_plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{"metrics"},
      .enabled_by_default = false,
      .factory = [] { return std::make_unique<metrics_plugin>(); },
   });
}

auto document = fcl::config::document{};
document.set("plugins.metrics.enabled", true);
document.set("plugins.api.enabled", false);
app.configure(document);
```

## Application Shell Example

The derived app declares only application-specific hooks. It does not store
`plugin_context`, `application_runtime`, diagnostics, events, signals and API registry
as repeated boilerplate members.

```cpp
struct service_config {
   std::uint16_t workers = 2;
};

BOOST_DESCRIBE_STRUCT(service_config, (), (workers))

template <>
struct fcl::schema::rules<service_config> {
   static fcl::schema::object_schema<service_config> define() {
      auto schema = fcl::schema::object<service_config>();
      schema.field<&service_config::workers>("workers").default_value(2).range(1, 64);
      return schema;
   }
};

class service_application final : public fcl::app::application_shell {
 public:
   service_application()
       : fcl::app::application_shell{fcl::app::application_shell_options{
            .name = "service",
            .runtime = {.worker_threads = 2, .thread_name = "service"},
         }} {}

 protected:
   void on_describe_config(fcl::config::component_registry& registry) const override {
      registry.add(fcl::config::describe_component<service_config>("service"));
   }

   boost::asio::awaitable<void> on_configure(fcl::app::configure_context& context) override {
      workers_ = context.view("service").get_or<std::uint16_t>("workers", 2);
      co_return;
   }

   void on_register_plugins(fcl::app::plugin_registry& registry) override {
      registry.register_plugin(fcl::app::plugin_descriptor{
         .id = fcl::app::plugin_id{"http"},
         .factory = [] {
            return std::make_unique<http_plugin>();
         },
      });
   }

   boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
      context.events().publish(
         fcl::app::event_severity::info,
         "service.configure",
         "worker slots: " + std::to_string(workers_));
      co_return;
   }

 private:
   std::uint16_t workers_ = 0;
};
```

## Application Builder Example

`application_builder` is the normal production path when callbacks are enough.
It does not define a second lifecycle: `build()` returns
`std::unique_ptr<fcl::app::application_shell>`, and the generated shell still
owns config merge, plugin lifecycle, rollback, API registry, events and
diagnostics.

```cpp
import fcl.app;
import fcl.asio.runtime;

auto workers = std::uint16_t{0};
auto builder = fcl::app::application_builder{};

builder.name("service")
   .runtime(fcl::asio::runtime_options{.worker_threads = 2, .thread_name = "service"})
   .config<service_config>("service", [&](const service_config& config) {
      workers = config.workers;
   })
   .provide([&](fcl::app::application_context& context) {
      context.events().publish(
         fcl::app::event_severity::info,
         "service.configure",
         "worker slots: " + std::to_string(workers));
   })
   .plugin(fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{"http"},
      .factory = [] {
         return std::make_unique<http_plugin>();
      },
   })
   .run_foreground([](fcl::app::application_shell&) {
      return 0;
   });

std::unique_ptr<fcl::app::application_shell> app = std::move(builder).build();
```

Use the subclass form only when the application has substantial state or
non-trivial lifecycle customization that the builder callbacks cannot express
cleanly.

## Running The Shell

Config can come from YAML, JSON, environment adapters or CLI. The shell only
receives a neutral `fcl::config::document`.

```cpp
import fcl.asio.blocking;
import fcl.config;

auto app = service_application{};

auto document = fcl::config::document{};
document.set("service.workers", 4U);
document.set("http.bind-port", 9090U);

app.configure(document);
fcl::asio::blocking::run(app.runtime(), app.startup());
app.request_stop();
fcl::asio::blocking::run(app.runtime(), app.shutdown());
```

`startup()` calls `initialize()` automatically when the shell is still in the
created state. Tests may call `initialize()` explicitly when they need to assert
API publication before plugin initialization.

For production foreground daemons prefer `run_application(...)`. It
standardizes the common flow: configure, startup, wait, request stop, shutdown.

```cpp
import fcl.app;
import fcl.config;

auto app = service_application{};
auto document = fcl::config::document{};
document.set("service.workers", 4U);

auto options = fcl::app::run_options{
   .handle_sigint = true,
   .handle_sigterm = true,
   .shutdown_timeout = std::chrono::seconds{10},
};

return fcl::app::run_application(app, document, options);
```

Tests and embedders can replace OS signals with a custom async waiter:

```cpp
options.handle_sigint = false;
options.handle_sigterm = false;
options.wait_for_stop = [](fcl::app::application_shell& app) -> boost::asio::awaitable<void> {
   auto timer = boost::asio::steady_timer{app.runtime().context()};
   timer.expires_after(std::chrono::milliseconds{50});
   co_await timer.async_wait(boost::asio::use_awaitable);
};
```

## Daemon Runner

`run_daemon(...)` is the recommended entrypoint for ordinary foreground
daemons. It owns the boring but risky glue that products otherwise rewrite:
collect app and plugin descriptors, load YAML, load an explicit `.env`, read
process environment, parse daemon and app/plugin CLI, merge defaults, handle
`--help`, `--check-config`, `--print-effective-config` and `--configure`, then
call `run_application(...)`.

```cpp
int main(int argc, char** argv) {
   return fcl::app::run_daemon(
      [](const fcl::app::daemon_context& context) {
         return std::make_unique<service_application>(service_application_options{
            .data_dir = context.data_dir,
            .profile = context.profile,
            .shell = context.shell,
         });
      },
      argc,
      argv,
      fcl::app::daemon_options{
         .name = "service",
         .display_name = "Service daemon",
         .default_data_dir_name = "service",
         .env_prefix = "SERVICE",
      });
}
```

The factory is intentional. `application_shell` creates its runtime and
scheduler in the constructor, so early daemon config such as
`daemon.runtime-threads`, `daemon.scheduler-queue-depth`, `daemon.data-dir` and
`daemon.profile` must be known before the application object exists.
For runtimes with two or more workers, the daemon derives the scheduler blocking
budget as `runtime-threads - 1`, leaving one worker available for timers and
awaitables. Single-thread mode remains valid for minimal daemons, but blocking
tasks that wait on the same scheduler can deadlock there.

Built-in daemon YAML:

```yaml
daemon:
   profile: dev_local
   data-dir: /home/user/.fcl/service
   config: /home/user/.fcl/service/config.yml
   dotenv: /home/user/.fcl/service/.env
   runtime-threads: 2
   scheduler-queue-depth: 4096
   shutdown-timeout-ms: 10000

service:
   workers: 4

plugins:
   http:
      enabled: true
```

The merge order is fixed:

```text
schema defaults < daemon defaults < YAML < .env < process env < daemon CLI < app/plugin CLI
```

Daemon bootstrap flags are always parsed: `--help`, `--profile`,
`--data-dir`, `--config`, `--dotenv`, runtime/scheduler flags and action flags.
Individual source adapters can be disabled with `daemon_options::read_yaml`,
`read_dotenv`, `read_process_env` and `read_cli`. An empty `env_prefix`
disables both `.env` and process env sources; `run_daemon(...)` never searches
parent directories for `.env`.

`--print-effective-config` and `--configure` write redacted documents using the
same registry. Secret fields from schema descriptors are never printed as real
values.

## Signal Bridge

`request_stop()` remains synchronous and `noexcept`; that makes it safe to call
from OS signal bridges and platform service callbacks. Use the runner for
normal foreground daemons; write a manual bridge only when embedding FCL into a
larger host runtime.

```cpp
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <csignal>
#include <memory>

auto app = service_application{};
auto signals = std::make_shared<boost::asio::signal_set>(
   app.runtime().context(),
   SIGINT,
   SIGTERM);

boost::asio::co_spawn(
   app.runtime().context(),
   [&app, signals]() -> boost::asio::awaitable<void> {
      co_await signals->async_wait(boost::asio::use_awaitable);
      app.request_stop();
   },
   boost::asio::detached);
```

## APIs

Plugin-to-plugin contracts use `fcl_api`: the application can publish
implementations during the `on_provide(...)` phase, plugins can publish
implementations during their `provide(...)` phase, and runtime consumers receive
a read-only API view. This keeps lifecycle in `fcl_app` and contract/version/error
semantics in `fcl_api`.

```cpp
#include <fcl/api/api_macros.hpp>

class cache : public fcl::api::contract<cache> {
 public:
   virtual ~cache() = default;
   virtual boost::asio::awaitable<models::chunk> read(protocol::read_chunk request) = 0;
};

FCL_API(cache, FCL_API_CONTRACT("cache", 1, 8), FCL_API_METHOD(read))

boost::asio::awaitable<void> on_provide(fcl::app::application_context& context) override {
   context.apis().install<cache>(std::make_shared<cache_impl>());
   co_return;
}

boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
   cache_ = context.apis().get<cache>({.id = {"cache"}, .major = 1, .min_revision = 8});
   co_return;
}
```

Infrastructure plugins should publish narrow APIs instead of leaking their
transport/runtime internals:

```cpp
boost::asio::awaitable<void> provide(fcl::api::provider& provider) override {
   provider.install<node_admin_api>(std::make_shared<node_admin_api_impl>(impl_));
   co_return;
}
```

The implementation type can stay in the plugin `.cpp`; consumers only see the
published API interface.

Do not use APIs as lifecycle modules. A plugin owns behavior/lifecycle; an API is
the typed contract exposed by that plugin or application.

## Events And Diagnostics

Events are for operator visibility. Diagnostics preserve lifecycle state and
last errors for app/plugin startup.

```cpp
auto subscription = app.events().subscribe({
   .topic = "app",
   .min_severity = fcl::app::event_severity::warning,
   .include_child_topics = true,
});

auto snapshot = app.diagnostics().snapshot(app.events());
for (const auto& plugin : snapshot.plugins) {
   if (!plugin.last_error.empty()) {
      report(plugin.id, plugin.last_error);
   }
}
```

## Startup Failure And Rollback

Plugin order comes from `plugin_registry` dependencies. Startup runs in that
order; shutdown runs in reverse order. If plugin `B` fails during startup after
plugin `A` started, the shell asks the runtime to shut down started plugins and
records diagnostics.

```cpp
#include <fcl/exceptions/macros.hpp>

try {
   app.configure(document);
   fcl::asio::blocking::run(app.runtime(), app.startup());
} FCL_CAPTURE_AND_RETHROW(
   "application startup failed",
   fcl::exceptions::ctx("component", "service"))
```

The app should still call `request_stop()` and `shutdown()` from the outer
entrypoint cleanup path. Both calls are idempotent enough for normal failure
handling.

The cleanup path is not optional ceremony. It keeps failed startup paths from
leaving APIs, background jobs or plugins in half-started states. Prefer
`run_application(...)` for normal foreground daemons because it centralizes that
flow.

## Lower-Level Escape Hatch

`application_runtime` remains available when a host framework already owns
runtime, API registry, signals and diagnostics. That is an escape hatch, not the normal
daemon pattern. Prefer `application_shell` or `application_builder` for new
services.

```cpp
auto runtime = fcl::app::application_runtime{context, std::move(plugins), &diagnostics};
boost::asio::awaitable<void> run_runtime(fcl::app::application_runtime& runtime) {
   co_await runtime.configure(document);
   co_await runtime.startup();
   co_await runtime.shutdown();
}
```

## Typical Mistakes

- Do not copy shell-owned members into every application.
- Do not use `application_builder` to define another lifecycle; it only creates
  a shell.
- Do not manually instantiate plugins in application startup code when
  `application_shell` can own the registry.
- Do not use APIs as fake plugins. Plugins own lifecycle and behavior; APIs
  expose typed contracts.
- Do not configure plugin options from `build_plugins()`; plugin config belongs
  to `plugin::describe_config()` and `plugin::configure(component_view)`.
- Do not parse `argv` or backend parser objects inside plugins.
- Do not wrap `run_daemon(...)` with another generic config framework. Application
  code should define typed config structs and plugin descriptors, not another
  merge engine.
- Do not put secrets into events or diagnostics without redaction first.
- Do not assume `request_stop()` waits for cleanup; it only requests shutdown.
- Do not stop the scheduler or `io_context` from a stop callback or hook.
  Plugins may still need async cleanup work during `shutdown()`.
- Do not ignore failed `initialize()` or `startup()`. Treat the shell as stopped
  and create a new instance if the application wants another attempt.
- Do not use `abort()` from signal handlers or lifecycle catches. Request stop,
  run shutdown at the application boundary, then return an error.

## Runtime Risks And Anti-Patterns

- A plugin that starts background work must own cancellation and shutdown.
  Hidden detached work is a process-lifetime leak.
- APIs must be provided before plugins initialize and must outlive plugin
  shutdown. Removing APIs early creates use-after-shutdown failures.
- Event subscribers should keep their connection lifetime explicit. Capturing a
  short-lived object in a long-lived signal/event callback is a common crash
  source.
- Events and diagnostics are observability surfaces, not recovery. Log/report
  them, but still propagate correctness-path failures to the caller.
- Lower-level `application_runtime` users must preserve the same lifecycle
  ordering as the shell. If they cannot, they should use `application_shell`
  instead of reimplementing daemon plumbing.

## Tests

`test_fcl_app` covers API publication, event bus bounds, plugin dependency order,
config collection, shell-owned default merge, configure-before-initialize,
startup rollback, reverse shutdown and diagnostics.

Executable lifecycle coverage lives in `test_fcl_app`. Consumer snippets stay in
this README and the runtime docs so they cannot drift behind an unbuilt example
tree.
