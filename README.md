# FCL

FCL — набор C++23 foundation-библиотек для проектов, которым нужны стабильная
сериализация, typed configuration, async runtime, network primitives, crypto,
logging and terminal UI без протаскивания продуктовой семантики в базовый слой.

Это уже не source-compatible копия старого FC. Историческая совместимость
сохраняется только там, где она является wire contract: например
`fcl::raw::pack` для поддерживаемых типов должен оставаться byte-to-byte
совместимым со старым `fc::raw::pack`. Исходные namespace `fc::...`,
`FC_REFLECT` and старые exception hierarchy не являются частью FCL API.

## Когда Использовать

- Нужен module-first C++23 toolkit с явными границами библиотек.
- Нужна бинарная совместимость raw serialization с FC/EOS-like контрактами, но
  без сохранения старого source API.
- Нужна единая схема config flow: `schema -> document -> YAML/JSON/CLI -> typed decode`.
- Нужен async runtime поверх Boost.Asio, где shutdown/backpressure являются
  частью API, а не afterthought.
- Нужны neutral HTTP/WebSocket/QUIC/P2P/TUI building blocks без downstream
  product vocabulary.

## Когда Не Использовать

- Если нужен drop-in replacement для исходников, которые включают `<fc/...>`.
- Если проект не готов к C++23 modules and Homebrew LLVM/modern Clang toolchain.
- Если нужна business-domain framework layer: FCL намеренно не знает о продуктовых
  ролях, storage policies, admin flows, billing or application-specific protocols.
- Если нужен browser UI, ORM, DI container or validation framework уровня
  Pydantic: FCL даёт schema/config diagnostics, но не превращает C++ в web framework.

## Быстрый Старт

```bash
cmake -S . -B build/fcl-debug -G Ninja \
  -DBUILD_TESTING=ON \
  -DFCL_ENABLE_MODULES=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk

cmake --build build/fcl-debug -j 1 --target fcl test_fcl
ctest --test-dir build/fcl-debug --output-on-failure
```

AppleClang не является целевым компилятором для module build. `import std;` в
baseline не используется; FCL импортирует свои modules and обычные system/vendor
headers через global module fragment.

## Минимальный Пример

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

struct http_config {
   std::string bind_host = "127.0.0.1";
   std::uint16_t bind_port = 8080;
   bool tls_enabled = false;
};

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_host, bind_port, tls_enabled))

import fcl.config;
import fcl.json;
import fcl.schema;

template <>
struct fcl::schema::rules<http_config> {
   static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&http_config::bind_port>("bind-port").default_value(8080).range(1, 65535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      return schema;
   }
};

auto parsed = fcl::json::read<http_config>(
   R"({"bind-host":"0.0.0.0","bind-port":9090,"tls-enabled":false})");
if (!parsed.ok()) {
   // typed diagnostics: path, code, severity, message
}
```

## Библиотеки

| Библиотека | Target | Что Делает | Основные Зависимости |
| --- | --- | --- | --- |
| [core](libraries/core/README.md) | `fcl_core` | Chrono helpers, strings, UTF-8, type names, `uint128`. | Boost headers/date_time/multiprecision as owned implementation detail. |
| [exception](libraries/exception/README.md) | `fcl_exception` | Std-based context errors and capture/assert macros. | `fcl_core`. |
| [reflect](libraries/reflect/README.md) | `fcl_reflect` | Thin Boost.Describe traversal helpers. | Boost.Describe via Boost headers. |
| [variant](libraries/variant/README.md) | `fcl_variant` | Dynamic value/object model and described conversions. | `fcl_core`, `fcl_reflect`, Boost.MultiIndex/multiprecision. |
| [raw](libraries/raw/README.md) | `fcl_raw` | Byte-compatible binary serialization. | `fcl_core`, `fcl_reflect`, `fcl_variant`, `fcl_exception`. |
| [json](libraries/json/README.md) | `fcl_json` | JSON typed/value/document codec over Glaze. | Glaze privately, `fcl_variant`, `fcl_config`, `fcl_schema`. |
| [yaml](libraries/yaml/README.md) | `fcl_yaml` | YAML typed/value/document codec with JSON-shaped API. | Glaze privately, `fcl_config`, `fcl_schema`. |
| [schema](libraries/schema/README.md) | `fcl_schema` | Field rules, defaults, ranges, diagnostics. | `fcl_reflect`. |
| [config](libraries/config/README.md) | `fcl_config` | Neutral config document, merge, decode, redaction. | `fcl_schema`. |
| [program_options](libraries/program_options/README.md) | `fcl_program_options` | CLI adapter from Boost.Program_options into config documents. | Boost.Program_options privately. |
| [crypto](libraries/crypto/README.md) | `fcl_crypto` | Hashes, encodings, keys, signatures, OpenSSL 3 crypto. | OpenSSL::Crypto, GMP, secp256k1, BLS. |
| [log](libraries/log/README.md) | `fcl_log` | Logging core, messages, console/appender boundary. | `fcl_variant`, Boost.DLL privately. |
| [asio](libraries/asio/README.md) | `fcl_asio` | Asio runtime, blocking boundary, priority scheduler. | Boost.Asio, threads. |
| [app](libraries/app/README.md) | `fcl_app` | Async application lifecycle, plugins, ports, diagnostics. | `fcl_asio`, `fcl_config`. |
| [http](libraries/http/README.md) | `fcl_http` | HTTP target/base URL, router, middleware, client/server. | Boost.Beast/URL/Asio, OpenSSL. |
| [websocket](libraries/websocket/README.md) | `fcl_websocket` | WebSocket connection/client primitives. | Boost.Beast/Asio, OpenSSL. |
| [quic](libraries/quic/README.md) | `fcl_quic` | QUIC endpoint, listener, connector, framed streams. | ngtcp2, OpenSSL 3, Boost.Asio. |
| [p2p](libraries/p2p/README.md) | `fcl_p2p` | Peer identity, sessions, protocols, relay, path manager. | `fcl_quic`, `fcl_asio`. |
| [tui](libraries/tui/README.md) | `fcl_tui` | Terminal UI value models, render helpers, runner. | Notcurses core privately and optionally. |

`fcl` is an all-in aggregate target for consumers that intentionally want the
whole library set. Production code that needs a small dependency footprint
should link concrete leaf targets such as `fcl_config`, `fcl_json` or
`fcl_quic`; external backends like OpenSSL, ngtcp2, Glaze and Boost components
belong to the leaf target that actually owns their API or implementation use.

## Архитектурные Документы

- [docs/index.md](docs/index.md) — карта документации.
- [docs/roadmap.md](docs/roadmap.md) — release readiness and migration gates.
- [docs/migration/storlane-fc-to-fcl.md](docs/migration/storlane-fc-to-fcl.md) — migration guide for downstream consumers.
- [docs/runtime/asio-app.md](docs/runtime/asio-app.md) — runtime, scheduler and async app lifecycle.
- [docs/web/http-websocket.md](docs/web/http-websocket.md) — HTTP/WebSocket layering.
- [docs/network/quic-p2p.md](docs/network/quic-p2p.md) — QUIC and P2P model.
- [docs/tui/notcurses-component-library.md](docs/tui/notcurses-component-library.md) — TUI abstraction over Notcurses.
- [docs/codecs/json-yaml-glaze.md](docs/codecs/json-yaml-glaze.md) — JSON/YAML codec boundary.
- [docs/config/schema-config-program-options.md](docs/config/schema-config-program-options.md) — schema/config/CLI flow.

README в `libraries/<lib>` является быстрым guide по конкретной библиотеке.
`/docs` хранит только сквозные решения, которые проходят через несколько
библиотек.

## Совместимость

- `fcl::raw::pack/unpack` сохраняет старый byte layout для retained primitive,
  chrono, string/container, variant/static_variant, described object and crypto
  wrapper cases, покрытых golden tests.
- Reflection canonical spelling — Boost.Describe. `FCL_REFLECT` and `FC_REFLECT`
  запрещены.
- Time API использует `std::chrono`; старые `fcl::time_point` aliases не
  возвращаются.
- Ошибки являются std-compatible: `context_error` используется только для
  structured context and nested exception chains.

## Security Baseline

- Secrets must be redacted before logging, JSON/YAML output or TUI rendering.
- Crypto generation and verification are in-process; shell-out crypto не является
  допустимым product path.
- TLS/QUIC/P2P verification errors are typed failures, not generic connection
  messages.
- UI and HTTP helpers are not authority boundaries. Authorization and signing
  decisions belong to the consuming product.

## Release Gates

```bash
cmake --build build/fcl-debug -j 1 \
  --target fcl test_fcl test_fcl_exception test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_asio test_fcl_app test_fcl_schema test_fcl_config test_fcl_yaml \
  test_fcl_program_options test_fcl_http_websocket test_fcl_quic_p2p test_fcl_tui

ctest --test-dir build/fcl-debug --output-on-failure
git diff --check
```

Static gates used during development:

```bash
rg "#include\\s*[<\"]fc/|namespace fc\\b|fc::|FC_REFLECT|FCL_REFLECT" libraries tests CMakeLists.txt cmake
find libraries -path '*/include/fcl/*/*' -type d -print
rg "glz::|YAML::Node|notcurses" libraries/*/include -g '*.cppm'
```

Expected result: no product hits, except explicitly documented macro-only
headers such as `libraries/exception/include/fcl/exception/macros.hpp`.

## Install And Consume

```bash
cmake --install build/fcl-debug --prefix build/fcl-install --component dev
```

Consumer CMake:

```cmake
find_package(FCL CONFIG REQUIRED)

target_link_libraries(my_program PRIVATE
   FCL::fcl_raw
   FCL::fcl_crypto
   FCL::fcl_app
   FCL::fcl_log
)
```

The repository also contains an external smoke project under
[`tests/package_consumer`](tests/package_consumer) that verifies installed
targets and module imports through `find_package(FCL CONFIG REQUIRED)`.
