# fcl_app

`fcl_app` provides an async application lifecycle and plugin coordination layer.
Plugins describe config, receive a typed config view, initialize in dependency
order, start, stop and shut down with rollback-friendly diagnostics.

## When To Use

- A program has multiple infrastructure plugins with ordered startup/shutdown.
- Plugins must publish config descriptors without knowing whether values came
  from YAML, JSON, environment or CLI.
- The application wants lifecycle diagnostics and an event bus.

## When Not To Use

- Do not use it as dependency injection for arbitrary business objects.
- Do not parse `argv` here; CLI parsing belongs to `fcl_program_options`.
- Do not put product-specific authority/security checks in the app core.

## Public Modules

- `fcl.app.plugin`, `fcl.app.plugin_context`, `fcl.app.plugin_registry`.
- `fcl.app.application` — `application_base`, `application_runtime`.
- `fcl.app.ports` — typed port registry.
- `fcl.app.events`, `fcl.app.diagnostics`, `fcl.app.signals`.
- `fcl.app` — aggregate import.

Target: `fcl_app`.

Dependencies: `fcl_asio`, `fcl_config`, Boost headers.

## Examples

### Wrap Runtime In `application_base`

Program binaries usually expose their own application class. It owns the FCL
runtime objects, delegates plugin lifecycle to `application_runtime`, and keeps
`request_stop()` synchronous so signal handlers can call it safely.

```cpp
#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

import fcl.app;
import fcl.asio.runtime;
import fcl.asio.task_scheduler;

class service_app final : public fcl::app::application_base {
public:
   explicit service_app(std::vector<std::unique_ptr<fcl::app::plugin>> plugins)
      : scheduler_{runtime_}
      , context_{scheduler_, ports_, signals_, events_, &diagnostics_}
      , app_{context_, std::move(plugins), &diagnostics_} {}

   boost::asio::awaitable<void> initialize() override {
      co_await app_.initialize();
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_await app_.startup();
      co_return;
   }

   void request_stop() noexcept override {
      app_.request_stop();
   }

   boost::asio::awaitable<void> shutdown() override {
      co_await app_.shutdown();
      scheduler_.stop();
      co_return;
   }

   fcl::asio::runtime& runtime() noexcept { return runtime_; }

private:
   fcl::asio::runtime runtime_{{.worker_threads = 2, .thread_name = "service"}};
   fcl::asio::task_scheduler scheduler_;
   fcl::app::port_registry ports_;
   fcl::app::signal_bus signals_;
   fcl::app::event_bus events_;
   fcl::app::diagnostics_store diagnostics_;
   fcl::app::plugin_context context_;
   fcl::app::application_runtime app_;
};
```

The class above is intentionally thin. Product code may add CLI/YAML loading,
PID files or platform service integration around it, but the plugin lifecycle
stays in `application_runtime`. Notice that `request_stop()` does not stop the
runtime directly: async `shutdown()` still needs the runtime to run cleanup.
The same rule applies to the scheduler: stop intent is synchronous, cleanup is
asynchronous, and queued cleanup work needs the scheduler to stay alive until
shutdown has completed.

Buildable versions of this pattern live in
[`examples/app/application_lifecycle.cpp`](../../examples/app/application_lifecycle.cpp)
and [`examples/app/exception_logging.cpp`](../../examples/app/exception_logging.cpp).

### Publish Config For A Plugin

```cpp
#include <boost/describe.hpp>

#include <cstdint>

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
      schema.field<&http_config::bind_port>("bind-port").default_value(8080).range(1, 65535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      return schema;
   }
};
```

### Implement A Plugin

```cpp
import fcl.app.plugin;
import fcl.config;

class http_plugin final : public fcl::app::plugin {
public:
   fcl::app::plugin_id id() const override { return {"http"}; }
   std::string version() const override { return "1"; }

   std::optional<fcl::config::component_descriptor> describe_config() const override {
      return fcl::config::describe_component<http_config>("http");
   }

   boost::asio::awaitable<void> configure(fcl::config::component_view view) override {
      port_ = view.get_or<std::uint16_t>("bind-port", 8080);
      co_return;
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context&) override { co_return; }
   boost::asio::awaitable<void> startup() override { co_return; }
   boost::asio::awaitable<void> shutdown() override { co_return; }

private:
   std::uint16_t port_ = 8080;
};
```

### Subscribe To Lifecycle Signals

`signal_bus` is a typed lifecycle notification channel. Use it for metrics,
diagnostics and operator-visible state. Do not use it as hidden business flow;
plugins should still communicate through ports or explicit APIs.

```cpp
import fcl.app.signals;

auto signals = fcl::app::signal_bus{};
auto started = signals.plugin_started.connect([](const fcl::app::plugin_signal& event) {
   record_metric("plugin.started", event.plugin);
});
auto stopped = signals.plugin_stopped.connect([](const fcl::app::plugin_signal& event) {
   record_metric("plugin.stopped", event.plugin);
});

// Keep returned boost::signals2::connection objects if you need to disconnect.
started.disconnect();
stopped.disconnect();
```

### Run A Foreground Program

`fcl_app` does not install OS signal handlers by itself. A binary decides how to
bridge `SIGINT`/`SIGTERM`, platform service stop requests or test hooks into
`request_stop()`.

```cpp
import fcl.app;
import fcl.asio.blocking;

auto app = service_app{make_plugins()};

try {
   fcl::asio::blocking::run(app.runtime(), app.initialize());
   fcl::asio::blocking::run(app.runtime(), app.startup());
   wait_for_process_signal([&] { app.request_stop(); });
   fcl::asio::blocking::run(app.runtime(), app.shutdown());
} catch (...) {
   app.request_stop();
   fcl::asio::blocking::run(app.runtime(), app.shutdown());
   throw;
}
```

The catch block is not optional ceremony. It keeps failed startup paths from
leaving ports, background jobs or plugins in half-started states.

### Bridge OS Signals Into `request_stop()`

The app layer keeps signal policy outside the framework, but it composes cleanly
with `boost::asio::signal_set` when a program wants POSIX-style foreground
shutdown.

```cpp
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <csignal>

import fcl.asio.runtime;

auto app = service_app{make_plugins()};
auto signals = boost::asio::signal_set{app.runtime().context(), SIGINT, SIGTERM};

boost::asio::co_spawn(
   app.runtime().context(),
   [&]() -> boost::asio::awaitable<void> {
      co_await signals.async_wait(boost::asio::use_awaitable);
      app.request_stop();
   },
   boost::asio::detached);
```

`request_stop()` must stay `noexcept` because signal bridges and platform service
callbacks need a safe synchronous edge. Cleanup still belongs to async
`shutdown()`.

### Emit Lifecycle Diagnostics From Signals

`signal_bus` is useful for readable lifecycle logs without coupling plugins to a
specific logger.

```cpp
auto connection = signals.plugin_stopped.connect(
   [&](const fcl::app::plugin_signal& event) {
      events.publish(
         fcl::app::event_severity::info,
         "plugin.lifecycle",
         event.plugin + " stopped");
   });
```

Keep the returned connection if the subscriber has a shorter lifetime than the
application.

### Install And Consume Ports

Ports are typed interfaces. They are how plugins share runtime services without
stringly-typed event coupling.

```cpp
import fcl.app.ports;

class clock_port {
public:
   virtual ~clock_port() = default;
   virtual std::chrono::system_clock::time_point now() const = 0;
};

context.ports().install<clock_port>(std::make_shared<system_clock_port>());

auto clock = context.ports().get<clock_port>();
auto now = clock->now();
```

### Publish Events Without Creating Business Flow

Events are for diagnostics and operator visibility. They should not replace
typed ports or direct API calls between components.

```cpp
import fcl.app.events;

context.events().publish(
   fcl::app::event_severity::info,
   "http.startup",
   "server is listening");

auto subscription = context.events().subscribe({
   .topic = "http",
   .min_severity = fcl::app::event_severity::warning,
   .include_child_topics = true,
});

while (auto event = subscription.poll()) {
   render_event(*event);
}
```

### Read Diagnostics Snapshot

```cpp
import fcl.app.diagnostics;

auto snapshot = diagnostics.snapshot(events);
for (const auto& plugin : snapshot.plugins) {
   if (!plugin.last_error.empty()) {
      report(plugin.id, plugin.last_error);
   }
}
```

### Runtime Flow

```cpp
import fcl.app;

auto runtime = fcl::app::application_runtime{context, std::move(plugins)};
auto registry = runtime.describe_config();
co_await runtime.configure(config_document);
co_await runtime.initialize();
co_await runtime.startup();
runtime.request_stop();
co_await runtime.shutdown();
```

### Handle Startup Failure With Rollback

`application_runtime::startup()` rolls back already-started plugins through
`shutdown()` when a later plugin fails. A program should still record the failure
and request stop before returning an error code.

```cpp
#include <fcl/exception/macros.hpp>

import fcl.app;
import fcl.exception.exception;

try {
   co_await runtime.configure(config_document);
   co_await runtime.initialize();
   co_await runtime.startup();
} FCL_CAPTURE_AND_RETHROW(
   "application startup failed",
   fcl::error::ctx("program", "service"))
```

The diagnostics store keeps the plugin lifecycle state, so operator-facing
tools can show which plugin failed and which plugins were shut down during
rollback.

### Compose Config From Adapters Outside `fcl_app`

```cpp
import fcl.config;
import fcl.program_options;
import fcl.yaml;

auto registry = runtime.describe_config();
auto file = fcl::yaml::load_document(config_path);
auto cli = fcl::program_options::parse(argc, argv, registry);

auto effective = fcl::config::merge({
   file.value,
   cli.document,
});

co_await runtime.configure(effective);
```

### Shape A Program Entrypoint

The recommended foreground shape is explicit: configure, initialize, startup,
wait for a stop signal, then shutdown. Tests can replace `wait_for_stop_signal`
with a deterministic hook.

```cpp
import fcl.asio.blocking;

int main(int argc, char** argv) {
   auto app = service_app{make_plugins()};

   try {
      fcl::asio::blocking::run(app.runtime(), app.initialize());
      fcl::asio::blocking::run(app.runtime(), app.startup());
      wait_for_stop_signal([&] { app.request_stop(); });
      fcl::asio::blocking::run(app.runtime(), app.shutdown());
      return 0;
   } catch (...) {
      app.request_stop();
      fcl::asio::blocking::run(app.runtime(), app.shutdown());
      throw;
   }
}
```

## Lifecycle Contract

Order is always:

1. collect config descriptors;
2. configure plugins;
3. initialize;
4. startup;
5. request stop;
6. shutdown.

Startup failure rolls back already-started plugins through shutdown where
possible and records diagnostics.

## Typical Mistakes

- Do not make plugin constructors perform I/O; use `initialize`.
- Do not assume `request_stop()` awaits cleanup; it is synchronous and noexcept.
- Do not stop the scheduler or `io_context` inside `request_stop()`. Plugins may
  still need async cleanup work during `shutdown()`.
- Do not keep parser-specific types in plugin APIs.
- Do not use the event bus for request/response control flow.
- Do not install broad concrete implementation classes as ports; expose narrow
  interfaces.
- Do not ignore failed `initialize()` or `startup()`. Treat them as terminal for
  that runtime instance unless the application explicitly documents recovery.
- Do not use `abort()` from signal handlers or lifecycle catches. Request stop,
  run shutdown at the product boundary, then return an error.

## Runtime Risks And Anti-Patterns

- A plugin that starts background work must also own cancellation and shutdown.
  Hidden detached work is a process-lifetime leak.
- Ports must be installed before plugins initialize and must outlive plugin
  shutdown. Removing ports early creates use-after-shutdown failures.
- Event subscribers should keep their connection lifetime explicit. Capturing a
  short-lived object in a long-lived signal/event callback is a common crash
  source.
- Lifecycle diagnostics are not recovery. Log/report them, but still propagate
  correctness-path failures to the caller.

## Tests

`test_fcl_app` covers port registry, event bus bounds, plugin ordering, config
collection, lifecycle rollback and diagnostics.
