# FCL Config/Schema + Program Options + Async App v1

## Goal

Build the first production-shaped FCL configuration foundation:

- `fcl_schema`: schema rules over described C++ types.
- `fcl_config`: neutral config document, merge, decode, redaction, component registry.
- `fcl_yaml`: YAML backend that exposes `config::document` without backend parser leakage.
- `fcl_program_options`: CLI adapter over `Boost.Program_options`.
- `fcl_app`: plugin config and lifecycle without direct CLI parser dependency.

## Architecture Decisions

- Parser backend types are visible only inside codec implementation files.
- `Boost.Program_options` is visible only inside `fcl_program_options`.
- App/plugin core imports `fcl_config`, not source parser backends. Later daemon-runner work may orchestrate YAML/env/CLI adapters at the foreground entrypoint layer.
- Plugins expose config through `describe_config()` and receive a `config::component_view` in `configure(...)`.
- Lifecycle methods that may touch resources return `boost::asio::awaitable<void>`.
- `request_stop()` remains synchronous and `noexcept`.

## Config Model

Merge order is fixed:

```text
schema defaults < config file < environment/custom adapters < CLI
```

`fcl_config::document` is the neutral representation for all sources. Backend adapters translate into that document:

- YAML file -> `fcl_yaml -> config::document`.
- CLI argv -> `fcl_program_options -> config::document`.
- Future environment/custom adapters -> `config::document`.

Typed decoding is done after merge with `config::decode<T>()`.

## Schema Rules

Schema rules are declared beside the consuming type:

```cpp
template <>
struct fcl::schema::rules<http_config> {
   static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port").required().default_value(8080).range(1, 65535);
      schema.field<&http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      schema.field<&http_config::token>("token").secret();
      return schema;
   }
};
```

Validation diagnostics carry path, code, severity, and message. Schema is not a parser and is not a migration framework yet.

## App Lifecycle

Old transitional shape:

```text
plugin -> set_program_options(boost::program_options::options_description&)
plugin -> initialize(...)
plugin -> startup()
plugin -> shutdown()
```

New shape:

```text
plugin -> describe_config()
plugin -> configure(config::component_view)
plugin -> initialize(...) -> awaitable<void>
plugin -> startup() -> awaitable<void>
plugin -> request_stop() noexcept
plugin -> shutdown() -> awaitable<void>
```

The application runtime collects config descriptors, applies config before initialize, starts plugins in order, and rolls back startup failures by shutting down initialized plugins in reverse order.

## Accepted Debt

- Generic enum conversion through Boost.Describe is sensitive to C++ module visibility. Public enums should be described in their owning module before conversion helpers are instantiated.
- Full config migration/version framework is intentionally out of scope.
- Profile-specific validation, richer aliases, and schema-generated help polish are future improvements.

## Verification

Required focused targets:

```sh
cmake --build build/fcl-config-schema-app-debug -j 1 \
  --target fcl test_fcl_schema test_fcl_config test_fcl_yaml test_fcl_program_options test_fcl_app

ctest --test-dir build/fcl-config-schema-app-debug \
  --output-on-failure \
  -R "test_fcl_schema|test_fcl_config|test_fcl_yaml|test_fcl_program_options|test_fcl_app" \
  --timeout 240
```

Static gates:

```sh
rg "boost/program_options|program_options" libraries/app
rg "glz::|glaze/" libraries/yaml/include libraries/json/include tests
rg "app_config|config_program_options" libraries tests docs AGENTS.md
git diff --check
```
