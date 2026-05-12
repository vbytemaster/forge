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

### Publish Config For A Plugin

```cpp
#include <boost/describe.hpp>

#include <cstdint>

struct http_config {
   std::uint16_t bind_port = 8080;
   bool tls_enabled = false;
};

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, tls_enabled))

import fcl.config;
import fcl.schema;

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
- Do not keep parser-specific types in plugin APIs.
- Do not use the event bus for request/response control flow.
- Do not install broad concrete implementation classes as ports; expose narrow
  interfaces.

## Tests

`test_fcl_app` covers port registry, event bus bounds, plugin ordering, config
collection, lifecycle rollback and diagnostics.
