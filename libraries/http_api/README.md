# forge_http_api

`forge_http_api` is the typed HTTP API layer on top of `forge_http` and `forge_api`.
It maps `FORGE_API(...)` contracts to native HTTP routes, builds typed server
bindings, and provides route-aware typed HTTP clients.

The C++ namespace is `forge::http::api`, mirroring `forge::api`:

```cpp
#include <forge/http_api/macros.hpp>

import forge.http.api.binding;
import forge.http.api.proxy;

auto http_binding = forge::http::api::binding()
   .use(forge::api::binding().serve(registry).build())
   .bind<cache_api>()
   .build();

auto cache = co_await forge::http::api::remote<cache_api>(client);
```

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

Target/component: `forge_http_api` / `http_api`.

Dependencies: `forge_http`, `forge_api`, `forge_json`, `forge_schema`.

`forge_http_api` may depend on `forge_http`; `forge_http` must not depend on
`forge_http_api`. WebSocket, QUIC and P2P API builders are intentionally unchanged
in this layer split.
