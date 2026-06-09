# FCL

FCL — нейтральный C++23 infrastructure framework и constructor toolkit для
проектов, которые собирают distributed infrastructure: DePIN-сети, то есть
децентрализованные сети физической инфраструктуры, blockchain/control-plane
системы, P2P runtimes, service daemons, plugin-based applications and
transport/API substrates.

FCL даёт строительные блоки, которые обычно приходится заново писать в каждом
серьёзном продукте: stable serialization, typed configuration, async runtime,
application shell, plugin contracts, API-over-transport, HTTP/WebSocket/QUIC/P2P,
crypto, logging, OTLP export and terminal UI. При этом FCL не переносит
downstream product vocabulary в публичный API: storage placement, billing,
authorization policy, Storlane/Spring semantics and application protocols живут
выше.

Это уже не source-compatible копия старого FC. Историческая совместимость
сохраняется только там, где она является wire contract: например
`fcl::raw::pack` для поддерживаемых типов должен оставаться byte-to-byte
совместимым со старым `fc::raw::pack`. Исходные namespace `fc::...`,
`FC_REFLECT` and старые exception hierarchy не являются частью FCL API.

## Когда Использовать

- Нужен module-first C++23 framework substrate с явными границами библиотек.
- Нужна бинарная совместимость raw serialization с FC/EOS-like контрактами, но
  без сохранения старого source API.
- Нужна единая схема config flow: `schema -> document -> YAML/JSON/env/CLI -> typed decode`.
- Нужен async runtime поверх Boost.Asio, где shutdown/backpressure являются
  частью API, а не afterthought.
- Нужны neutral HTTP/WebSocket/QUIC/P2P/API/plugin/TUI building blocks для
  DePIN, blockchain and distributed service construction без downstream product
  vocabulary.

## Когда Не Использовать

- Если нужен drop-in replacement для исходников, которые включают `<fc/...>`.
- Если проект не готов к C++23 modules and Homebrew LLVM/modern Clang toolchain.
- Если нужна business-domain layer: FCL намеренно не знает о продуктовых ролях,
  storage policies, admin flows, billing or application-specific protocols.
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
| [exceptions](libraries/exceptions/README.md) | `fcl_exceptions` | Std-based context errors and capture/assert macros. | `fcl_core`. |
| [reflect](libraries/reflect/README.md) | `fcl_reflect` | Thin Boost.Describe traversal helpers. | Boost.Describe via Boost headers. |
| [variant](libraries/variant/README.md) | `fcl_variant` | Dynamic value/object model and described conversions. | `fcl_core`, `fcl_reflect`, Boost.MultiIndex/multiprecision. |
| [raw](libraries/raw/README.md) | `fcl_raw` | Byte-compatible binary serialization. | `fcl_core`, `fcl_reflect`, `fcl_variant`, `fcl_exceptions`. |
| [json](libraries/json/README.md) | `fcl_json` | JSON typed/value/document codec over Glaze. | Glaze privately, `fcl_variant`, `fcl_config`, `fcl_schema`. |
| [yaml](libraries/yaml/README.md) | `fcl_yaml` | YAML typed/value/document codec with JSON-shaped API. | Glaze privately, `fcl_config`, `fcl_schema`. |
| [schema](libraries/schema/README.md) | `fcl_schema` | Field rules, defaults, ranges, diagnostics. | `fcl_reflect`. |
| [config](libraries/config/README.md) | `fcl_config` | Neutral config document, merge, decode, redaction. | `fcl_schema`. |
| [program_options](libraries/program_options/README.md) | `fcl_program_options` | CLI adapter from Boost.Program_options into config documents. | Boost.Program_options privately. |
| [env](libraries/env/README.md) | `fcl_env` | Process env and explicit `.env` adapter into config documents. | `fcl_config`, `fcl_schema`. |
| [api](libraries/api/README.md) | `fcl_api` | Typed local/remote API contracts, handles, descriptors and frame vocabulary. | `fcl_exceptions`, `fcl_raw`. |
| [api_transport](libraries/api_transport/README.md) | `fcl_api_transport` | API frames over reusable transport streams/sessions. | `fcl_api`, `fcl_raw`, `fcl_transport`. |
| [crypto](libraries/crypto/README.md) | `fcl_crypto` | Hashes, encodings, keys, signatures, OpenSSL 3.0+ crypto. | OpenSSL::Crypto, GMP, secp256k1, BLS. |
| [log](libraries/log/README.md) | `fcl_log` | Logging core, messages, console/appender boundary. | `fcl_variant`, Boost.DLL privately. |
| [otlp](libraries/otlp/README.md) | `fcl_otlp` | OTLP/HTTP JSON log export and crash-spool resend. | `fcl_log`, `fcl_http`, `fcl_asio`. |
| [asio](libraries/asio/README.md) | `fcl_asio` | Asio runtime, blocking boundary, priority scheduler. | Boost.Asio, threads. |
| [app](libraries/app/README.md) | `fcl_app` | Opinionated application shell, plugins, ports, config and diagnostics. | `fcl_asio`, `fcl_config`. |
| [http](libraries/http/README.md) | `fcl_http` | HTTP target/base URL, router, middleware, client/server. | Boost.Beast/URL/Asio, OpenSSL. |
| [websocket](libraries/websocket/README.md) | `fcl_websocket` | WebSocket connection/client primitives. | Boost.Beast/Asio, OpenSSL. |
| [transport](libraries/transport/README.md) | `fcl_transport` | Reusable stream/session concepts, chunk buffers and frame helpers. | Boost.Asio, `fcl_exceptions`. |
| [tcp](libraries/tcp/README.md) | `fcl_tcp` | TCP transport adapter over `fcl_transport`. | Boost.Asio, `fcl_transport`. |
| [stcp](libraries/stcp/README.md) | `fcl_stcp` | Secure TCP transport profile. | `fcl_tcp`, `fcl_crypto`, `fcl_transport`. |
| [yamux](libraries/yamux/README.md) | `fcl_yamux` | Yamux multiplexed sessions over a transport stream. | `fcl_transport`, Boost.Asio. |
| [quic](libraries/quic/README.md) | `fcl_quic` | QUIC endpoint, listener, connector, framed streams. | ngtcp2, OpenSSL 3.0+, Boost.Asio. |
| [multiformats](libraries/multiformats/README.md) | `fcl_multiformats` | libp2p-compatible varint, multicodec, multihash, multibase and multiaddr. | `fcl_crypto`, `fcl_exceptions`. |
| [p2p](libraries/p2p/README.md) | `fcl_p2p` | Peer identity, sessions, discovery, relay, DHT, rendezvous and GossipSub. | `fcl_transport`, `fcl_multiformats`, `fcl_quic`, `fcl_yamux`. |
| [plugins](libraries/plugins/README.md) | `fcl_plugins` | Infrastructure plugins: P2P node, API resolver, diagnostics and PubSub facade. | `fcl_app`, `fcl_api`, `fcl_p2p`. |
| [tui](libraries/tui/README.md) | `fcl_tui` | Terminal UI value models, render helpers, runner. | Notcurses core privately and optionally. |

`find_package(FCL CONFIG REQUIRED)` is intentionally lightweight and discovers
only the `core` package surface. Production code that needs feature libraries
must request components and then link concrete leaf targets such as
`FCL::fcl_config`, `FCL::fcl_env`, `FCL::fcl_json` or `FCL::fcl_quic`. External backends like
OpenSSL, ngtcp2, Glaze and Boost components belong to the leaf target that
actually owns their API or implementation use. `FCL::fcl` remains the all-in
aggregate target, but consumers should request `COMPONENTS all` before linking
it.

## Архитектурные Документы

- [docs/index.md](docs/index.md) — карта документации.
- [docs/roadmap.md](docs/roadmap.md) — release readiness and migration gates.
- [docs/runtime/asio-app.md](docs/runtime/asio-app.md) — runtime, scheduler and async app lifecycle.
- [docs/web/http-websocket.md](docs/web/http-websocket.md) — HTTP/WebSocket layering.
- [docs/network/quic-p2p.md](docs/network/quic-p2p.md) — QUIC and P2P model.
- [docs/blueprints/blockchain-constructor/README.md](docs/blueprints/blockchain-constructor/README.md) — planning map for FCL as a neutral constructor substrate.
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
   --target fcl test_fcl test_fcl_exceptions test_fcl_raw test_fcl_json test_fcl_crypto \
  test_fcl_multiformats test_fcl_asio test_fcl_transport test_fcl_tcp test_fcl_stcp \
  test_fcl_yamux test_fcl_quic test_fcl_app test_fcl_schema test_fcl_config \
  test_fcl_yaml test_fcl_program_options test_fcl_env test_fcl_api \
  test_fcl_api_transport test_fcl_http_websocket test_fcl_quic_p2p \
  test_fcl_plugins test_fcl_otlp test_fcl_tui

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
headers such as `libraries/exceptions/include/fcl/exceptions/macros.hpp`.

## Install And Consume

```bash
cmake --install build/fcl-debug --prefix build/fcl-install --component dev
```

Consumer CMake:

```cmake
find_package(FCL CONFIG REQUIRED COMPONENTS raw crypto app log)

target_link_libraries(my_program PRIVATE
   FCL::fcl_raw
   FCL::fcl_crypto
   FCL::fcl_app
   FCL::fcl_log
)
```

The repository also contains external smoke projects under
[`tests/package_consumer`](tests/package_consumer) and
[`tests/package_core_lightweight`](tests/package_core_lightweight). They verify
both component-rich consumers and the lightweight `find_package(FCL)` path that
does not discover heavy crypto/transport/codec backends.

## License

FCL is licensed under the Apache License, Version 2.0.

Copyright (c) 2026 Vladimir Tarnakin.

FCL is intended to be a reusable infrastructure framework for open-source and
commercial software. Downstream products may use separate licensing terms for
their product layers.

Canonical upstream:
https://github.com/vbytemaster/fcl
