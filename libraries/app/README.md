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

## Tests

`test_fcl_app` covers port registry, event bus bounds, plugin ordering, config
collection, lifecycle rollback and diagnostics.
