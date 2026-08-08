# forge_api_http

`forge_api_http` binds `FORGE_API(...)` contracts to native HTTP routes. It sits
above `forge_net_http` and `forge_api_core`: the lower layer owns HTTP mechanics, while
this library owns typed request/response mapping, codec dispatch and typed
clients.

## When To Use

- Expose a described Forge API contract over HTTP route/path/status semantics.
- Build route-aware typed clients for the same contract.
- Use JSON by default and opt into XML per route for typed DTO bodies.
- Keep native HTTP escape hatches such as file, stream, bytes, empty,
  body-stream, body-bytes and multipart routes.

## When Not To Use

- Do not use `forge_api_http` for raw streaming endpoints that do not have a
  typed API contract. Use `forge_net_http` directly.
- Do not put server bind, TLS, socket lifecycle or application auth policy in
  this library. Those belong to `forge_net_http`, plugins or consumers.
- Do not force every API into one generic RPC endpoint. Use explicit HTTP
  routes and statuses.

## Public Modules

- `forge.api.http.parameters` — HTTP request DTO wrapper parameters and special
  response types in namespace `forge::api::http`; transport primitives remain
  in `forge::net::http`.
- `forge.api.http.mapping` — route metadata, `traits<T>`, route template parsing
  and rendering metadata.
- `forge.api.http.binding` — `binding_builder`, `binding_plan`, `binding()` and
  server-side mount.
- `forge.api.http.client_request` — typed proxy request construction internals.
- `forge.api.http.client_response` — typed proxy response materialization and
  bounded error decode internals.
- `forge.api.http.proxy` — `remote<T>()` and remote invoker glue.
- `forge.api.http.openapi` — OpenAPI 3.1 generation from the same typed contract
  and route descriptors used by the HTTP binding.

Macro header: `<forge/api/http/macros.hpp>`.

## Target And Component

- CMake target: `forge_api_http`
- Package target: `Forge::forge_api_http`
- Package component: `api_http`

## Dependencies

- `forge_net_http`
- `forge_api_core`
- `forge_codec_json`
- `forge_codec_xml`
- `forge_raw`
- `forge_reflect`
- `forge_schema`
- Boost.Asio

## Examples

### Bind A Contract

```cpp
#include <forge/api/http/macros.hpp>

import forge.api.core.binding;
import forge.api.http.binding;
import forge.api.http.proxy;

auto local = forge::api::core::binding().serve(registry).build();
auto http_binding = forge::api::http::binding()
   .use(std::move(local))
   .bind<catalog_api>()
   .build();

auto remote = co_await forge::api::http::remote<catalog_api>(client);
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

`forge_api_http` accepts a missing `Content-Type` as the route's configured
request codec for compatibility with existing JSON routes. Explicit mismatches
still fail with `415 Unsupported Media Type`. Typed DTO responses check `Accept`
before invoking the handler when the emitted codec is not acceptable.

## Security And Boundaries

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
- Do not make `forge_net_http` depend on `forge_api_http`; dependency direction is
  `forge_api_http -> forge_net_http`.
- Do not use positional HTTP arguments for large envelopes. Use a described
  request DTO when the body or validation surface grows.

## Tests

- `test_forge_api_core`
- `test_forge_http_websocket`
- `test_forge_package_api_http_component`
