# FCL Release-To-Storlane Iteration v1

## Summary

This pass turns FCL into a packageable release candidate for downstream
integration. It does not change any downstream repository or submodule pointer.

The focus is practical adoption:

- production-shaped `fcl_app` examples;
- richer `fcl_asio` and `fcl_exception` documentation;
- synchronous logger v2 with structured records, sinks, redaction and stacktrace
  snapshots;
- CMake install/export package;
- external consumer smoke test through `find_package(FCL CONFIG REQUIRED)`;
- migration guide from old FC-style APIs to final FCL APIs.

## Logger v2 Boundary

Accepted:

- sync console/file/JSONL sinks;
- cheap level checks before expensive field providers;
- source location, chrono timestamp, component/logger name and thread metadata;
- explicit secret fields rendered as `<redacted>`;
- automatic stacktrace snapshot for error logs when a backend is available;
- exception-chain routing through `fcl::exception::set_log_sink`.

Rejected:

- logger-owned runtime, background queue or async policy;
- public `std::stacktrace` or `boost::stacktrace` types;
- logs as audit/security authority;
- product-specific trace schema inside FCL.

Stacktrace backend priority is `std::stacktrace`, then private
Boost.Stacktrace basic, then `stacktrace_unavailable`.

## CMake Package Shape

The installed package exports `FCL::` targets:

- leaf targets such as `FCL::fcl_raw`, `FCL::fcl_crypto`, `FCL::fcl_app`;
- aggregate `FCL::fcl` for consumers that intentionally want the whole feature
  set;
- vendor support targets required by exported leaf targets.

Downstream code should link leaf targets by default. The aggregate target is a
convenience target, not a dependency minimization strategy.

## Buildable Examples

`examples/app/application_lifecycle.cpp` demonstrates:

- `application_shell`;
- plugin registry;
- typed ports;
- config descriptor and `component_view`;
- lifecycle events and diagnostics;
- signal bridge through `boost::asio::signal_set`;
- explicit stop/shutdown sequence.

`examples/app/exception_logging.cpp` demonstrates:

- `FCL_CAPTURE_AND_LOG`;
- exception chain formatting;
- redaction;
- routing exception capture into `fcl_log`.

## Release Gates

Required before downstream integration:

```bash
cmake --build build/fcl-release-hardening-debug -j 1 \
  --target fcl test_fcl test_fcl_exception test_fcl_log test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_asio test_fcl_app test_fcl_schema test_fcl_config test_fcl_yaml \
  test_fcl_program_options test_fcl_http_websocket test_fcl_quic_p2p test_fcl_tui \
  fcl_example_app_lifecycle fcl_example_exception_logging

ctest --test-dir build/fcl-release-hardening-debug --output-on-failure \
  -R "^(test_fcl|test_fcl_exception|test_fcl_log|test_fcl_raw|test_fcl_json|test_fcl_crypto|test_fcl_asio|test_fcl_app|test_fcl_schema|test_fcl_config|test_fcl_yaml|test_fcl_program_options|test_fcl_http_websocket|test_fcl_quic_p2p|test_fcl_tui|test_fcl_example_app_lifecycle|test_fcl_example_exception_logging|test_fcl_package_install|test_fcl_package_consumer)$" \
  --timeout 360
```

Static gates:

```bash
rg "import fcl\\.asio|fcl_asio|task_scheduler" libraries/log
rg "boost::stacktrace|std::stacktrace" libraries/log/include tests/package_consumer examples
rg "legacy FC logging macro names" libraries tests examples docs README.md AGENTS.md
git diff --check
```

## Remaining Boundary

This pass prepares FCL for downstream use, but it does not perform downstream
migration. The downstream repository should only switch after package smoke,
full regression and review are green.
