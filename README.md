# FORGE

FORGE — нейтральный C++23 infrastructure framework и constructor toolkit для
проектов, которые собирают distributed infrastructure: DePIN-сети, то есть
децентрализованные сети физической инфраструктуры, blockchain/control-plane
системы, P2P runtimes, service daemons, plugin-based applications and
transport/API substrates.

FORGE даёт строительные блоки, которые обычно приходится заново писать в каждом
серьёзном продукте: stable serialization, typed configuration, async runtime,
application shell, plugin contracts, API-over-transport, HTTP/WebSocket/QUIC/P2P,
crypto, logging, OTLP export and terminal UI. При этом FORGE не переносит
downstream product vocabulary в публичный API: storage placement, billing,
authorization policy, Storlane/Spring semantics and application protocols живут
выше.

Это уже не source-compatible копия старого FC. Историческая совместимость
сохраняется только там, где она является wire contract: например
`forge::raw::pack` для поддерживаемых типов должен оставаться byte-to-byte
совместимым со старым `fc::raw::pack`. Исходные namespace `fc::...`,
`FC_REFLECT` and старые exception hierarchy не являются частью FORGE API.

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
- Если нужна business-domain layer: FORGE намеренно не знает о продуктовых ролях,
  storage policies, admin flows, billing or application-specific protocols.
- Если нужен browser UI, ORM, DI container or validation framework уровня
  Pydantic: FORGE даёт schema/config diagnostics, но не превращает C++ в web framework.

## Быстрый Старт

```bash
cmake -S . -B build/forge-debug -G Ninja \
  -DBUILD_TESTING=ON \
  -DFORGE_ENABLE_MODULES=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk

cmake --build build/forge-debug -j 1 --target forge test_forge
ctest --test-dir build/forge-debug --output-on-failure
```

AppleClang не является целевым компилятором для module build. `import std;` в
baseline не используется; FORGE импортирует свои modules and обычные system/vendor
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

import forge.json;
import forge.schema.object;

template <>
struct forge::schema::rules<http_config> {
   static forge::schema::object_schema<http_config> define() {
      auto schema = forge::schema::object<http_config>();
      schema.field<&http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&http_config::bind_port>("bind-port").default_value(8080).range(1, 65535);
      schema.field<&http_config::tls_enabled>("tls-enabled").default_value(false);
      return schema;
   }
};

auto parsed = forge::json::read<http_config>(
   R"({"bind-host":"0.0.0.0","bind-port":9090,"tls-enabled":false})");
if (!parsed.ok()) {
   // typed diagnostics: path, code, severity, message
}
```

## Библиотеки

| Библиотека | Target | Что Делает | Основные Зависимости |
| --- | --- | --- | --- |
| [core](libraries/core/README.md) | `forge_core` | Chrono helpers, strings, UTF-8, type names, `uint128`. | Boost headers/date_time/multiprecision as owned implementation detail. |
| [exceptions](libraries/exceptions/README.md) | `forge_exceptions` | Std-based context errors and capture/assert macros. | `forge_core`. |
| [reflect](libraries/reflect/README.md) | `forge_reflect` | Thin Boost.Describe traversal helpers. | Boost.Describe via Boost headers. |
| [variant](libraries/variant/README.md) | `forge_variant` | Dynamic value/object model and described conversions. | `forge_core`, `forge_reflect`, Boost.MultiIndex/multiprecision. |
| [raw](libraries/raw/README.md) | `forge_raw` | Byte-compatible binary serialization. | `forge_core`, `forge_reflect`, `forge_variant`, `forge_exceptions`. |
| [json](libraries/json/README.md) | `forge_json` | JSON typed/value/document codec over Glaze. | Glaze privately, `forge_variant`, `forge_config`, `forge_schema`. |
| [yaml](libraries/yaml/README.md) | `forge_yaml` | YAML typed/value/document codec with JSON-shaped API. | Glaze privately, `forge_config`, `forge_schema`. |
| [schema](libraries/schema/README.md) | `forge_schema` | Field rules, defaults, ranges, diagnostics. | `forge_reflect`. |
| [config](libraries/config/README.md) | `forge_config` | Neutral config document, merge, decode, redaction. | `forge_schema`. |
| [program_options](libraries/program_options/README.md) | `forge_program_options` | CLI adapter from Boost.Program_options into config documents. | Boost.Program_options privately. |
| [env](libraries/env/README.md) | `forge_env` | Process env and explicit `.env` adapter into config documents. | `forge_config`, `forge_schema`. |
| [api](libraries/api/README.md) | `forge_api` | Typed local/remote API contracts, handles, descriptors and frame vocabulary. | `forge_exceptions`, `forge_raw`. |
| [transport_api](libraries/transport_api/README.md) | `forge_transport_api` | API frames over reusable transport streams/sessions. | `forge_api`, `forge_raw`, `forge_transport`. |
| [crypto](libraries/crypto/README.md) | `forge_crypto` | Hashes, encodings, keys, signatures, OpenSSL 3.0+ crypto. | OpenSSL::Crypto, GMP, secp256k1, BLS. |
| [log](libraries/log/README.md) | `forge_log` | Logging core, messages, console/appender boundary. | `forge_variant`, Boost.DLL privately. |
| [otlp](libraries/otlp/README.md) | `forge_otlp` | OTLP/HTTP JSON log export and crash-spool resend. | `forge_log`, `forge_http`, `forge_asio`. |
| [asio](libraries/asio/README.md) | `forge_asio` | Asio runtime, blocking boundary, priority scheduler. | Boost.Asio, threads. |
| [app](libraries/app/README.md) | `forge_app` | Opinionated application shell, plugins, ports, config and diagnostics. | `forge_asio`, `forge_config`. |
| [http](libraries/http/README.md) | `forge_http` | HTTP target/base URL, router, middleware, client/server. | Boost.Beast/URL/Asio, OpenSSL. |
| [websocket](libraries/websocket/README.md) | `forge_websocket` | WebSocket connection/client primitives. | Boost.Beast/Asio, OpenSSL. |
| [transport](libraries/transport/README.md) | `forge_transport` | Reusable stream/session concepts, chunk buffers and frame helpers. | Boost.Asio, `forge_exceptions`. |
| [tcp](libraries/tcp/README.md) | `forge_tcp` | TCP transport adapter over `forge_transport`. | Boost.Asio, `forge_transport`. |
| [stcp](libraries/stcp/README.md) | `forge_stcp` | Secure TCP transport profile. | `forge_tcp`, `forge_crypto`, `forge_transport`. |
| [yamux](libraries/yamux/README.md) | `forge_yamux` | Yamux multiplexed sessions over a transport stream. | `forge_transport`, Boost.Asio. |
| [quic](libraries/quic/README.md) | `forge_quic` | QUIC endpoint, listener, connector, framed streams. | ngtcp2, OpenSSL 3.0+, Boost.Asio. |
| [multiformats](libraries/multiformats/README.md) | `forge_multiformats` | libp2p-compatible varint, multicodec, multihash, multibase and multiaddr. | `forge_crypto`, `forge_exceptions`. |
| [p2p](libraries/p2p/README.md) | `forge_p2p` | Peer identity, sessions, discovery, relay, DHT, rendezvous and GossipSub. | `forge_transport`, `forge_multiformats`, `forge_quic`, `forge_yamux`. |
| [plugins](plugins/README.md) | `forge_plugins`, `forge_plugins_*_*` | Official infrastructure plugins: P2P node, API resolver, diagnostics, PubSub facade, crypto signer and crypto secrets. | `forge_app`, `forge_api`, focused plugin targets. |
| [tui](libraries/tui/README.md) | `forge_tui` | Terminal UI value models, render helpers, runner. | Notcurses core privately and optionally. |

`find_package(Forge CONFIG REQUIRED)` is intentionally lightweight and discovers
only the `core` package surface. Production code that needs feature libraries
must request components and then link concrete leaf targets such as
`Forge::forge_config`, `Forge::forge_env`, `Forge::forge_json` or `Forge::forge_quic`. External backends like
OpenSSL, ngtcp2, Glaze and Boost components belong to the leaf target that
actually owns their API or implementation use. `Forge::forge` remains the all-in
aggregate target, but consumers should request `COMPONENTS all` before linking
it.

## Архитектурные Документы

- [docs/index.md](docs/index.md) — карта документации.
- [docs/roadmap.md](docs/roadmap.md) — release readiness and migration gates.
- [docs/runtime/asio-app.md](docs/runtime/asio-app.md) — runtime, scheduler and async app lifecycle.
- [docs/web/http-websocket.md](docs/web/http-websocket.md) — HTTP/WebSocket layering.
- [docs/network/quic-p2p.md](docs/network/quic-p2p.md) — QUIC and P2P model.
- [docs/blueprints/blockchain-constructor/README.md](docs/blueprints/blockchain-constructor/README.md) — planning map for FORGE as a neutral constructor substrate.
- [docs/tui/notcurses-component-library.md](docs/tui/notcurses-component-library.md) — TUI abstraction over Notcurses.
- [docs/codecs/json-yaml-glaze.md](docs/codecs/json-yaml-glaze.md) — JSON/YAML codec boundary.
- [docs/config/schema-config-program-options.md](docs/config/schema-config-program-options.md) — schema/config/CLI flow.

README в `libraries/<lib>` является быстрым guide по конкретной библиотеке.
`/docs` хранит только сквозные решения, которые проходят через несколько
библиотек.

## Совместимость

- `forge::raw::pack/unpack` сохраняет старый byte layout для retained primitive,
  chrono, string/container, variant/static_variant, described object and crypto
  wrapper cases, покрытых golden tests.
- Reflection canonical spelling — Boost.Describe. `FORGE_REFLECT` and `FC_REFLECT`
  запрещены.
- Time API использует `std::chrono`; старые `forge::time_point` aliases не
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
cmake --build build/forge-debug -j 1 \
   --target forge test_forge test_forge_exceptions test_forge_raw test_forge_json test_forge_crypto \
  test_forge_multiformats test_forge_asio test_forge_transport test_forge_tcp test_forge_stcp \
  test_forge_yamux test_forge_quic test_forge_app test_forge_schema test_forge_config \
  test_forge_yaml test_forge_program_options test_forge_env test_forge_api \
  test_forge_transport_api test_forge_http_websocket test_forge_quic_p2p \
  test_forge_plugins test_forge_otlp test_forge_tui

ctest --test-dir build/forge-debug --output-on-failure
git diff --check
```

Static gates used during development:

```bash
rg "#include\\s*[<\"]fc/|namespace fc\\b|fc::|FC_REFLECT|FORGE_REFLECT" libraries tests CMakeLists.txt cmake
find libraries -path '*/include/forge/*/*' -type d -print
rg "glz::|YAML::Node|notcurses" libraries/*/include -g '*.cppm'
```

Expected result: no product hits, except explicitly documented macro-only
headers such as `libraries/exceptions/include/forge/exceptions/macros.hpp`.

## Install And Consume

```bash
cmake --install build/forge-debug --prefix build/forge-install --component dev
```

Consumer CMake:

```cmake
find_package(Forge CONFIG REQUIRED COMPONENTS raw crypto app log)

target_link_libraries(my_program PRIVATE
   Forge::forge_raw
   Forge::forge_crypto
   Forge::forge_app
   Forge::forge_log
)
```

The repository also contains external smoke projects under
[`tests/package_consumer`](tests/package_consumer) and
[`tests/package_core_lightweight`](tests/package_core_lightweight). They verify
both component-rich consumers and the lightweight `find_package(Forge)` path that
does not discover heavy crypto/transport/codec backends.

## License

FORGE is licensed under the Apache License, Version 2.0.

Copyright (c) 2026 Vladimir Tarnakin.

FORGE is intended to be a reusable infrastructure framework for open-source and
commercial software. Downstream products may use separate licensing terms for
their product layers.

Canonical upstream:
https://github.com/vbytemaster/forge
