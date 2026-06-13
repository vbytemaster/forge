# Концепция FCL — модульные базовые библиотеки для современных C++‑продуктов

Дата: 2026-05-09
Язык стандарта: **C++23**
Основной стиль: **C++20/23 modules first**
Рабочее название: **FCL — Foundation Core Libraries**

---

## 1. Краткая идея

**FCL** — это нейтральный набор базовых C++‑библиотек для построения современных системных, сетевых и серверных продуктов.

FCL не должен быть привязан к Storlane, Spring, storage, cloud, blockchain или конкретному продукту. Storlane должен стать одним из потребителей FCL, но не источником имени и не частью публичной модели FCL.

FCL должен дать разработчику удобный набор инструментов для:

- описания типов и сериализации;
- бинарной совместимости со старым FC raw byte layout;
- работы с YAML/JSON и валидацией конфигураций;
- криптографии;
- асинхронного выполнения;
- HTTP/WebSocket/QUIC;
- P2P‑сетей;
- жизненного цикла приложений и плагинов;
- логов, диагностики, ошибок и TUI;
- backend‑сервисов, CLI‑утилит, операторских консолей и сетевых daemon‑ов.

Цель по удобству: **для C++ это должно ощущаться не хуже, чем FastAPI для Python**: быстро описал типы, быстро поднял endpoint, быстро подключил async‑операции, получил понятную валидацию, ошибки, логи и lifecycle.

---

## 2. FCL — это библиотеки или фреймворк?

Публично FCL лучше позиционировать как:

> **модульный набор базовых библиотек с дополнительным лёгким слоем для сборки приложений.**

То есть FCL — не монолитный framework, который навязывает архитектуру продукта. Но внутри FCL может быть слой `fcl.app`, который даёт удобные primitives для lifecycle, plugins, ports, events и diagnostics.

Правильная формулировка:

> **FCL — Foundation Core Libraries: модульные C++‑библиотеки для системных и сетевых приложений, с опциональным app‑слоем.**

Это важно: пользователь может взять только `fcl.crypto` или `fcl.raw`, не подключая P2P, TUI или app runtime.

---

## 3. Главные принципы

### 3.1 Нейтральность

В FCL запрещены продуктовые понятия:

- `storlane`;
- `spring`;
- `workspace`;
- `mountd`;
- `contentd`;
- `repaird`;
- `directoryd`;
- `grant`;
- `acl`;
- `provider`;
- `obligation`;
- `receipt`;
- `storage assignment`;
- `repair finding`.

Эти понятия должны жить в Storlane или других продуктах.

В FCL допустимы нейтральные понятия:

- `runtime`;
- `task`;
- `scheduler`;
- `endpoint`;
- `connection`;
- `stream`;
- `peer`;
- `protocol`;
- `message`;
- `session`;
- `plugin`;
- `port`;
- `event`;
- `diagnostics`;
- `vault`;
- `key`;
- `certificate`;
- `config`;
- `schema`.

### 3.2 Module first

Публичный API FCL должен быть C++‑module API.

`.hpp` допускаются только для:

- макросов;
- preprocessor glue;
- C ABI/platform shims, если без них нельзя;

В текущем структурном проходе разрешены только macro-only `.hpp` в public `include/fcl`. Compatibility headers, legacy `fc` source API and macro‑обёртки над Boost.Describe не являются целевым состоянием.

Обычные публичные API не должны быть `.hpp`.

### 3.3 Маленькие независимые targets

FCL не должен быть одним большим target. Каждый крупный слой — отдельная библиотека:

- `fcl_core`;
- `fcl_reflect`;
- `fcl_raw`;
- `fcl_yaml`;
- `fcl_json`;
- `fcl_crypto`;
- `fcl_runtime`;
- `fcl_log`;
- `fcl_app`;
- `fcl_http`;
- `fcl_websocket`;
- `fcl_quic`;
- `fcl_p2p`;
- `fcl_tui`.

### 3.4 Pimpl и стабильность ABI

Для тяжёлых классов, которые владеют ресурсами, соединениями, сокетами, крипто‑контекстом, event loop или внешними библиотеками, использовать `pimpl`.

Примеры кандидатов на `pimpl`:

- `fcl::quic::connection`;
- `fcl::quic::listener`;
- `fcl::p2p::node`;
- `fcl::http::server`;
- `fcl::app::runtime`;
- `fcl::tui::screen`;
- `fcl::crypto::vault`;
- `fcl::log::logger`.

Цель: скрыть детали реализации, уменьшить rebuild surface и не протаскивать OpenSSL/ngtcp2/Boost internals в публичные interfaces.

### 3.5 Корутины и async API

Тяжёлые операции должны иметь async‑API на базе:

```cpp
boost::asio::awaitable<T>
```

Это касается:

- network connect/listen/accept/read/write;
- HTTP server/client;
- WebSocket;
- QUIC;
- P2P protocol streams;
- file/vault operations, если они потенциально блокирующие;
- background jobs;
- app startup/shutdown, если они запускают сеть или внешние ресурсы.

Синхронные wrappers допустимы, но они не должны быть единственным API.

---

## 4. Reflection и описание типов

### 4.1 Production reflection через Boost.Describe

FCL должен использовать **Boost.Describe** как основной способ описания сериализуемых типов.

Пример:

```cpp
#include <boost/describe.hpp>

struct user_config {
   std::string name;
   std::uint32_t max_connections = 0;
   bool enabled = true;
};

BOOST_DESCRIBE_STRUCT(user_config, (), (name, max_connections, enabled))
```

Причины:

- совместимость с внешним Boost ecosystem;
- меньше собственной macro‑магии;
- один reflection description можно использовать для бинарной сериализации, YAML, JSON, debug print, hash, сравнения и schema generation;
- FCL становится нейтральнее и меньше зависит от старой FC‑идентичности.

### 4.2 Отказ от FC reflect как основной модели

FC reflect не должен быть canonical reflection FCL.

Целевое состояние:

```text
новый код: Boost.Describe напрямую
совместимость: raw byte layout, а не source-level старой reflection-модели
```

---

## 5. Бинарная сериализация и совместимость со старым FC byte layout

### 5.1 Цель

Сохранить новый FCL API:

```cpp
fcl::raw::pack(value)
fcl::raw::unpack<T>(bytes)
```

Но внутренне перевести сериализацию на FCL implementation:

```cpp
fcl::raw::pack(value)
fcl::raw::unpack<T>(bytes)
```

### 5.2 Требование byte‑for‑byte compatibility

Для типов, которые уже сериализовались через FC, надо доказать:

```text
old FC bytes(T) == new FCL bytes(T)
```

Это hard gate.

Нужны golden tests:

- primitive types;
- strings;
- vectors;
- maps;
- optional;
- enums;
- described structs;
- nested structs;
- inherited structs;
- template structs;
- product‑critical protocol types.

### 5.3 Boost.Describe traversal order

Для described struct сериализация должна идти в явном порядке, заданном в `BOOST_DESCRIBE_STRUCT`.

Для derived types надо явно определить порядок:

```text
base classes first, in declared order;
then local members, in declared order.
```

Нельзя случайно поменять порядок из‑за другого descriptor traversal.

### 5.4 Enum compatibility

FC enum conversion имел свою семантику. FCL должен явно определить:

- enum → string;
- string → enum;
- int → enum;
- поведение unknown value;
- поведение numeric string compatibility path, если оно нужно для compatibility.

---

## 6. YAML, JSON и валидация через Boost.Describe

### 6.1 Можно ли сделать YAML converter через Boost.Describe?

Да. Boost.Describe даёт список полей, имена и pointers‑to‑members. Этого достаточно, чтобы построить generic YAML loader/emitter.

Пример целевого API:

```cpp
import fcl.yaml;
import fcl.config.key_path;
import fcl.config.value;
import fcl.config.document;
import fcl.config.component;
import fcl.config.decode;
import fcl.config.migration;

auto loaded = fcl::yaml::load_document("config.yml");
auto decoded = fcl::config::decode<server_config>(loaded.value, "server");
fcl::yaml::save_document("effective.yml", fcl::config::redact(loaded.value, registry));
```

Или:

```cpp
auto parsed = fcl::yaml::read_document(text);
auto config = fcl::config::decode<server_config>(parsed.value, "server");
```

### 6.2 Boost.Describe не является валидатором

Boost.Describe даёт metadata о структуре:

- какие поля есть;
- как они называются;
- где pointer‑to‑member;
- какие enum values описаны.

Но он не знает:

- обязательное поле или нет;
- минимальное/максимальное значение;
- regex;
- secret ли поле;
- можно ли задавать поле в production profile;
- deprecated ли поле;
- какие значения запрещены вместе.

Поэтому validation layer должен быть FCL‑owned.

### 6.3 Предлагаемый слой `fcl.schema`

Добавить отдельный слой:

```cpp
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;
```

Пример идеи:

```cpp
template<>
struct fcl::schema::rules<server_config> {
   static auto define() {
      auto schema = fcl::schema::object<server_config>();
      schema.field<&server_config::bind_host>("bind-host").required();
      schema.field<&server_config::bind_port>("bind-port").default_value(8080).range(1, 65535);
      schema.field<&server_config::private_key>("private-key").secret();
      schema.field<&server_config::worker_threads>("worker-threads").range(1, 256);
      return schema;
   }
};
```

Макро-обёртки над Boost.Describe не являются целевым API. Canonical spelling — прямой `BOOST_DESCRIBE_*` для описания типа и `fcl::schema::rules<T>` для правил.

### 6.4 YAML diagnostics

YAML loader должен возвращать понятные ошибки:

```text
config.yml:12: bind-port: expected integer 1..65535
config.yml:18: private-key: forbidden in production profile; use vault key ref
config.yml:24: unknown option "wroker-threads"; did you mean "worker-threads"?
```

Ошибки должны быть std-based и не зависеть от старой FC-иерархии исключений.

### 6.5 Redaction

Схема должна уметь помечать secret fields:

```text
private key
token
passphrase
workspace secret
storage handle
recovery material
vault passphrase
```

`fcl.yaml` / `fcl.config` должны уметь печатать effective config with redaction:

```cpp
auto redacted = fcl::config::redact(effective_document, registry);
std::cout << fcl::yaml::write_document(redacted).text << "\n";
```

### 6.6 JSON/YAML backend

Glaze — единый backend для `fcl_json` и `fcl_yaml`. FCL остаётся владельцем public API, `fcl::variant`, `fcl::config::document`, schema diagnostics и redaction. Glaze types, reflection metadata и backend parser details не выходят в public module interfaces.

`Boost.Program_options` — только backend `fcl_program_options`. Он переводит `argv` в `fcl::config::document`; `fcl_app` и plugins не видят `options_description` или `variables_map`.

Merge order фиксируется так:

```text
schema defaults < config file < environment/custom adapters < CLI
```

Публичный API должен быть:

```cpp
import fcl.json;
import fcl.yaml;
```

`fcl::json::read_value/write_value` and `fcl::yaml::read_value/write_value` work with `fcl::variant`.
`read_document/write_document` work with `fcl::config::document`.
`read<T>/write<T>` are typed codec functions over schema/Boost.Describe metadata.

### 6.7 JSON

Тот же reflection/schema слой должен работать для JSON:

```cpp
import fcl.json;

auto json_text = fcl::json::write(value).text;
auto value = fcl::json::read<T>(json_text).value;
```

Цель: заменить старую FC‑зависимость от JSON reflection более универсальной схемой.

---

## 7. Исключения и error handling

### 7.1 Проблема старого подхода

Старый capture-подход FC удобен, но был привязан к собственной иерархии исключений.

Для FCL нужен похожий по удобству механизм, но основанный на `std::exception`, `std::source_location`, structured context и typed error categories.

Статус: foundation реализован. В FCL v1 единственный собственный тип ошибки — `fcl::exceptions::context_error`, который наследуется от `std::runtime_error` и нужен только для структурированного контекста, redaction и nested exception chain.

### 7.2 Целевые свойства

Новые макросы/утилиты должны:

- добавлять контекст;
- сохранять исходное исключение;
- поддерживать `std::source_location`;
- работать со `std::runtime_error`, `std::system_error`, typed errors;
- не требовать старую FC-иерархию исключений;
- позволять логировать structured fields;
- быть безопасными для redaction.

### 7.3 Возможный API

```cpp
try {
   co_await node.async_start();
} FCL_CAPTURE_AND_RETHROW("starting p2p node",
   fcl::exceptions::ctx("peer", peer_id),
   fcl::exceptions::ctx("endpoint", endpoint)
)
```

Или без макроса:

```cpp
try {
   do_work();
} catch (...) {
   std::throw_with_nested(
      fcl::exceptions::context_error{
         "failed to start node",
         fcl::source_location::current(),
         {{"peer", peer_id}}
      });
}
```

### 7.4 Redaction в ошибках

Context fields должны иметь тип:

```cpp
fcl::ctx("token", value, fcl::sensitive)
```

или schema‑based redaction.

---

## 8. Криптография

### 8.1 Сохранить и очистить FC crypto

Сохранить полезные crypto primitives из `fc`:

- sha1/sha224/sha256/sha512;
- ripemd160;
- sha3/blake2, если нужны;
- public/private keys;
- signatures;
- K1;
- R1/WebAuthn;
- BLS, если нужен downstream;
- random;
- base58/hex.

Но новый namespace должен быть FCL:

```cpp
import fcl.crypto.sha256;
import fcl.crypto.asymmetric;
```

Crypto namespace lives under `fcl::crypto`; старый FC source namespace больше не является целью после structural split.

### 8.2 Перенести generic crypto из Storlane

Из Storlane перенести:

- AES‑256‑GCM;
- KDF;
- secure random helpers;
- crypto byte types;
- vault primitives, если они достаточно generic.

### 8.3 OpenSSL 3+

FCL должен использовать **OpenSSL 3+**.

Для QUIC можно требовать более строгую версию, если это нужно из‑за ngtcp2/OpenSSL QUIC integration.

Правило:

```text
один OpenSSL provider во всём build graph
никакого BoringSSL в FCL
никакого shell-out openssl для key/cert generation
```

---

## 9. Runtime и async

### 9.1 Перенести Storlane ASIO layer

Перенести из Storlane:

- runtime ownership;
- blocking helper;
- task scheduler;
- cancellation handles;
- bounded queues;
- delayed tasks;
- metrics.

Новый namespace:

```cpp
import fcl.asio.runtime;
import fcl.app.exceptions;
import fcl.app.application;
import fcl.app.events;
import fcl.app.diagnostics;
import fcl.app.signals;
import fcl.app.plugin_context;
import fcl.app.plugin;
import fcl.app.plugin_registry;
import fcl.app.application_shell;
import fcl.app.application_builder;
import fcl.app.runner;
import fcl.app.daemon;
```

### 9.2 API style

Сетевые и тяжёлые операции:

```cpp
boost::asio::awaitable<T>
```

Синхронные helpers допустимы:

```cpp
auto result = fcl::runtime::blocking::run(runtime, some_awaitable());
```

Но основной API должен быть async.

---

## 10. Network

### 10.1 HTTP

Перенести и очистить:

- HTTP client;
- HTTP server;
- router;
- middleware;
- route context;
- target/base URL helpers.

Цель:

```cpp
import fcl.http.exceptions;
import fcl.http.types;
import fcl.http.base_url;
import fcl.http.target;
import fcl.http.route_context;
import fcl.http.middleware;
import fcl.http.connection;
import fcl.http.client;
import fcl.http.router;
import fcl.http.server;
import fcl.http.api;
```

Пример:

```cpp
fcl::http::router router;
router.get("/healthz", [](auto& ctx) {
   return ctx.json({{"ok", true}});
});

boost::asio::awaitable<void> run_server() {
   co_await server.listen(endpoint);
}
```

### 10.2 WebSocket

Перенести:

- client;
- connection;
- ordered writes;
- close handshake;
- ping/pong;
- TLS support.

### 10.3 QUIC

Перенести:

- endpoint;
- connector;
- listener;
- connection;
- stream;
- framed stream;
- security;
- metrics;
- OpenSSL/ngtcp2 engine.

QUIC должен быть optional target:

```cmake
FCL_ENABLE_QUIC=ON
```

### 10.4 P2P

Перенести role‑agnostic P2P:

- peer id;
- session;
- peer store;
- protocol id;
- protocol stream open;
- peer exchange;
- direct path;
- relay;
- reachability;
- hole punching if ready;
- typed errors;
- metrics.

Не переносить Storlane overlay protocol.

FCL P2P должен позволять:

```cpp
import fcl.p2p.exceptions;
import fcl.p2p.identity;
import fcl.p2p.endpoint;
import fcl.p2p.envelope;
import fcl.p2p.identify;
import fcl.p2p.diagnostics;
import fcl.p2p.discovery;
import fcl.p2p.dht;
import fcl.p2p.rendezvous;
import fcl.p2p.pubsub;
import fcl.p2p.reachability;
import fcl.p2p.hole_punch;
import fcl.p2p.protocol;
import fcl.p2p.message;
import fcl.p2p.scoring;
import fcl.p2p.relay;
import fcl.p2p.resource_manager;
import fcl.p2p.stream;
import fcl.p2p.negotiation;
import fcl.p2p.peer_store;
import fcl.p2p.node;
import fcl.p2p.api;

node.register_protocol("/my/product/1", handler);
boost::asio::awaitable<void> open_product_stream() {
   auto open = node.open_protocol(peer, "/my/product/1");
   use_stream(co_await std::move(open));
}
```

---

## 11. App layer

### 11.1 Что переносить

Перенести абстрактные modules из Storlane app:

- ports;
- plugin;
- plugin context;
- plugin registry;
- app runtime;
- events;
- signals;
- diagnostics.

### 11.2 Что изменить

- убрать жёсткую привязку base plugin API к Boost.Program_options;
- оставить CLI parsing в отдельном adapter target `fcl_program_options`;
- plugins описывают конфиг через `describe_config()` и получают typed view через `configure(...)`;
- lifecycle для plugins async: `initialize/startup/shutdown -> boost::asio::awaitable<void>`;
- `request_stop()` остаётся sync/noexcept;
- оставить `fcl.app` optional;
- не делать app layer обязательным framework‑ом.

### 11.3 Цель

Удобно собирать daemon/backend/TUI/P2P приложения:

```cpp
fcl::app::ports ports;
ports.install<my_port>(...);

fcl::app::runtime app{context, plugins};
boost::asio::awaitable<void> run_app() {
   co_await app.configure(effective_config);
   co_await app.initialize();
   co_await app.startup();
   // ...
   app.request_stop();
   co_await app.shutdown();
}
```

---

## 12. TUI

Перенести базовые TUI primitives:

- navigation stack;
- screen runner;
- forms;
- table/list rendering;
- event log rendering;
- redaction;
- keyboard input abstraction;
- headless test mode.

TUI должен быть нейтральным:

```cpp
import fcl.tui.exceptions;
import fcl.tui.types;
import fcl.tui.render;
import fcl.tui.navigation;
import fcl.tui.runner;
```

Не переносить Storlane operator screens и Spring roles.

---

## 13. Config, YAML, logging, diagnostics

### 13.1 Config

Перенести generic идеи:

- data directory layout;
- config file discovery;
- effective config;
- redacted printing;
- first‑run configuration helper;
- last_error file writer;
- diagnostics file writer.

Не переносить Storlane deployment/enrollment/Spring semantics.

### 13.2 Logging

Перенести/очистить:

- console logging;
- file logging;
- JSONL logging;
- component log levels;
- structured fields;
- redaction;
- exception logging guard.

---

## 14. Архитектура каталогов

Обязательный паттерн:

```text
libraries/{lib}/include/fcl/{lib}/*.cppm      # public module interfaces
libraries/{lib}/include/fcl/{lib}/*macros.hpp # только macro-only исключения, если нужны
libraries/{lib}/*.cpp                         # implementation
libraries/{lib}/*_private.hpp                 # private implementation headers
libraries/{lib}/CMakeLists.txt
```

Пример:

```text
libraries/crypto/
  include/fcl/crypto/types.cppm
  include/fcl/crypto/random.cppm
  include/fcl/crypto/aes.cppm
  crypto.cpp
  random.cpp
  aes.cpp
  CMakeLists.txt
```

### 14.1 Почему `.cppm` в `include`

Этот проект выбирает module-first pattern: публичные modules лежат в `include/fcl/{lib}/*.cppm`, а implementation — рядом в `libraries/{lib}/*.cpp`.

Публичные `.hpp` в `include/fcl` не являются целевым состоянием, кроме macro-only файлов. Причина: C++ modules не экспортируют макросы. Такие `.hpp` не должны содержать public types/templates/functions и не могут быть header-wrapper слоем для `.cppm`.

### 14.2 Владение module BMI

`BMI` — Binary Module Interface, служебный файл компилятора для C++ modules. Целевое состояние: каждый доменный target (`fcl_core`, `fcl_raw`, `fcl_json`, `fcl_crypto` и т. д.) сам владеет своими `.cppm` через `FILE_SET CXX_MODULES`.

Общий `fcl_modules` bridge удалён. Он был допустим только как временная миграционная подпорка для старого монолитного графа, но не является архитектурным слоем FCL.

Правило теперь жёсткое: если при добавлении новой библиотеки появляется обратная зависимость или цикл импортов, надо разрывать домены, переносить типы вниз или выделять отдельный target. Маскировать цикл общим BMI-pool запрещено.

---

## 15. CMake targets

Примерный список:

```text
fcl_core
fcl_reflect
fcl_raw
fcl_yaml
fcl_json
fcl_crypto
fcl_runtime
fcl_log
fcl_config
fcl_app
fcl_http
fcl_websocket
fcl_quic
fcl_p2p
fcl_tui
```

Опции:

```cmake
FCL_ENABLE_MODULES=ON
FCL_ENABLE_QUIC=ON
FCL_ENABLE_P2P=ON
FCL_ENABLE_TUI=ON
FCL_ENABLE_YAML=ON
FCL_ENABLE_TESTS=ON
```

---

## 16. Тесты

### 16.1 Замена тестов под новые библиотеки

Все старые тесты должны быть перенесены под новые targets:

```text
tests/unit/raw
tests/unit/yaml
tests/unit/json
tests/unit/crypto
tests/unit/runtime
tests/unit/http
tests/unit/websocket
tests/unit/quic
tests/unit/p2p
tests/unit/app
tests/unit/tui
tests/unit/fcl_compat
```

### 16.2 Обязательные тестовые группы

#### Raw compatibility

- byte‑for‑byte golden tests;
- old FC critical vectors;
- enum behavior;
- nested/derived described types.

#### Boost.Describe integration

- `BOOST_DESCRIBE_STRUCT`;
- `BOOST_DESCRIBE_CLASS`;
- private/protected member cases if supported;
- enums;
- missing description failure.

#### YAML/JSON

- decode/encode roundtrip;
- unknown fields;
- required fields;
- type mismatch;
- range constraints;
- redaction;
- profile constraints.

#### Crypto

- AES‑GCM vectors;
- hash vectors;
- key/signature vectors;
- no shell‑out.

#### Runtime

- scheduler order;
- cancellation;
- delayed tasks;
- shutdown;
- queue bounds.

#### Network

- HTTP client/server;
- WebSocket close/write ordering;
- QUIC handshake, frame, timeout, backpressure;
- P2P direct, peer exchange, relay, protocol streams.

#### App

- plugin order;
- dependency validation;
- reverse shutdown;
- diagnostics;
- events.

#### TUI

- render headless;
- redaction;
- navigation;
- form validation.

---

## 17. Миграционный план

### Этап 1 — FCL skeleton

- создать repo/branch FCL;
- включить C++23;
- настроить CMake;
- создать `fcl_core`;
- добавить module‑first conventions;
- добавить architecture doc.

### Этап 2 — reflect/raw compatibility

- Boost.Describe как основной reflection layer;
- `fcl.raw`;
- old FC raw byte layout compatibility;
- golden tests.

### Этап 3 — crypto

- перенести FC crypto;
- перенести Storlane AES/KDF/random;
- OpenSSL 3+;
- tests.

### Этап 4 — runtime

- перенести Storlane ASIO runtime/scheduler;
- tests.

### Этап 5 — config/yaml/json/log

- YAML/JSON через Boost.Describe;
- schema/validation;
- redaction;
- logging/diagnostics.
- текущий v1 split: `fcl_schema`, `fcl_config`, `fcl_yaml`, `fcl_program_options`.

### Этап 6 — app

- ports/plugins/events/diagnostics;
- opinionated `application_shell`, который владеет runtime/scheduler/ports/events/signals/diagnostics/plugin registry;
- async cleanup.
- текущий v1 cleanup: `fcl_app` больше не зависит от `Boost.Program_options`, plugin lifecycle стал async, production entrypoint задаётся `application_shell`.

### Этап 7 — network

- HTTP;
- WebSocket;
- QUIC;
- P2P.

### Этап 8 — TUI

- neutral rendering/input/navigation;
- headless tests.

### Этап 9 — Storlane migration

- заменить local Storlane libraries на FCL targets;
- удалить дубли;
- оставить compatibility shims на переходный период.

---

## 18. Что могло быть забыто

Проверить отдельно:

- install/export CMake for modules;
- FetchContent сценарий;
- package versioning;
- symbol visibility;
- Windows support;
- Linux/macOS CI;
- sanitizer builds;
- fuzz tests для raw/yaml/quic/p2p codec;
- docs examples;
- error redaction policy;
- typed error categories;
- ABI/version policy;
- dependency policy;
- optional BLS/secp256k1 feature flags;
- deterministic serialization policy;
- schema evolution policy;
- module aggregate naming.

---

## 19. Критерии готовности FCL v1

FCL v1 можно считать готовым, когда:

1. новый код использует Boost.Describe для описания типов;
2. old FC raw byte layout compatibility доказана golden tests;
3. crypto tests зелёные на OpenSSL 3+;
4. runtime/scheduler стабилен;
5. YAML/JSON decode/encode/validation работают через described types;
6. HTTP/WebSocket/QUIC/P2P tests зелёные;
7. app lifecycle/plugin tests зелёные;
8. TUI headless tests зелёные;
9. Storlane может заменить свои local foundation libraries на FCL;
10. FCL не содержит Storlane/Spring/storage domain concepts.

---

## 20. Короткое резюме для агента

Нужно создать **FCL — Foundation Core Libraries**: C++23 module‑first набор базовых библиотек.

Ключевые решения:

- основной reflection — Boost.Describe;
- old FC raw byte layout compatibility сохранить;
- FC crypto сохранить и очистить;
- FC reflect как canonical API выпилить или оставить только в compatibility;
- YAML/JSON/validation строить поверх Boost.Describe + FCL schema rules;
- исключения — std-based, с контекстом и redaction, без зависимости от старой FC-иерархии;
- перенести из Storlane generic библиотеки: asio/runtime, http, websocket, quic, p2p, app, crypto, tui, config/yml/log;
- OpenSSL 3+;
- C++23;
- modules first;
- pimpl;
- async API на `boost::asio::awaitable`;
- каталоговый паттерн: `libraries/{lib}/include/fcl/{lib}/.../*.cppm` и `libraries/{lib}/*.cpp`;
- каждый layer — отдельный target;
- продуктовая семантика Storlane не переносится.

FCL должен позволять удобно создавать современные C++‑продукты: backend‑сервисы, P2P‑сети, TUI‑приложения, CLI‑утилиты, async API и сериализуемые конфиги.
