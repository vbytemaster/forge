# forge_http_api

`forge_http_api` binds `FORGE_API(...)` contracts to native HTTP routes. It sits
above `forge_http` and `forge_api`: the lower layer owns HTTP mechanics, while
this library owns typed request/response mapping, codec dispatch and typed
clients.

## When To Use

- Expose a described Forge API contract over HTTP route/path/status semantics.
- Build route-aware typed clients for the same contract.
- Use JSON by default and opt into XML per route for typed DTO bodies.
- Keep native HTTP escape hatches such as file, stream, bytes, empty,
  body-stream, body-bytes and multipart routes.

## When Not To Use

- Do not use `forge_http_api` for raw streaming endpoints that do not have a
  typed API contract. Use `forge_http` directly.
- Do not put server bind, TLS, socket lifecycle or application auth policy in
  this library. Those belong to `forge_http`, plugins or consumers.
- Do not force every API into one generic RPC endpoint. Use explicit HTTP
  routes and statuses.

## Public Modules

- `forge.http.api.parameters` — HTTP request DTO wrapper parameters and special
  response types that remain in namespace `forge::http`.
- `forge.http.api.mapping` — route metadata, `traits<T>`, route template parsing
  and rendering metadata.
- `forge.http.api.binding` — `binding_builder`, `binding_plan`, `binding()` and
  server-side mount.
- `forge.http.api.client_request` — typed proxy request construction internals.
- `forge.http.api.client_response` — typed proxy response materialization and
  bounded error decode internals.
- `forge.http.api.proxy` — `remote<T>()` and remote invoker glue.

Macro header: `<forge/http_api/macros.hpp>`.

## Target And Component

- CMake target: `forge_http_api`
- Package target: `Forge::forge_http_api`
- Package component: `http_api`

## Dependencies

- `forge_http`
- `forge_api`
- `forge_json`
- `forge_xml`
- `forge_raw`
- `forge_reflect`
- `forge_schema`
- Boost.Asio

## Examples

### Bind A Contract

```cpp
#include <forge/http_api/macros.hpp>

import forge.api.binding;
import forge.http.api.binding;
import forge.http.api.proxy;

auto local = forge::api::binding().serve(registry).build();
auto http_binding = forge::http::api::binding()
   .use(std::move(local))
   .bind<catalog_api>()
   .build();

auto remote = co_await forge::http::api::remote<catalog_api>(client);
```

### Select XML For A DTO Route

Routes use JSON request, response and error bodies by default. Per-route codec
options can opt into XML for typed DTO bodies while preserving native response
bypasses.

```cpp
FORGE_HTTP_API(catalog_api,
   FORGE_HTTP_PUT(update_item, "/items/:id", ok,
      FORGE_HTTP_REQUEST_BODY(xml),
      FORGE_HTTP_RESPONSE_BODY(xml),
      FORGE_HTTP_ERROR_BODY(xml)))
```

`forge_http_api` accepts a missing `Content-Type` as the route's configured
request codec for compatibility with existing JSON routes. Explicit mismatches
still fail with `415 Unsupported Media Type`. Typed DTO responses check `Accept`
before invoking the handler when the emitted codec is not acceptable.

## Boundaries And Safety

- JSON/XML DTO codecs apply only to typed DTO bodies. `file_response`,
  `streaming_response`, `stream_response`, `bytes_response`, `empty_response`,
  `body_stream`, `body_bytes` and multipart/form-data bypass DTO codecs.
- Error bodies use the route error codec and the shared Forge API error payload
  shape.
- Do not log request bodies, headers or query strings before redaction.
- Keep protocol-specific error names, signing, authorization and storage policy
  outside this library.

## Common Mistakes

- Do not add route options that do not change runtime behavior and tests.
- Do not decode native stream/file/bytes responses through JSON or XML.
- Do not make `forge_http` depend on `forge_http_api`; dependency direction is
  `forge_http_api -> forge_http`.
- Do not use positional HTTP arguments for large envelopes. Use a described
  request DTO when the body or validation surface grows.

## Tests

- `test_forge_api`
- `test_forge_http_websocket`
- `test_forge_package_http_api_component`
