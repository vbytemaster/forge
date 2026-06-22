# fcl_http_api

`fcl_http_api` is the typed HTTP API layer on top of `fcl_http` and `fcl_api`.
It maps `FCL_API(...)` contracts to native HTTP routes, builds typed server
bindings, and provides route-aware typed HTTP clients.

The C++ namespace is `fcl::http::api`, mirroring `fcl::api`:

```cpp
#include <fcl/http_api/macros.hpp>

import fcl.http.api.binding;
import fcl.http.api.proxy;

auto http_binding = fcl::http::api::binding()
   .use(fcl::api::binding().serve(registry).build())
   .bind<cache_api>()
   .build();

auto cache = co_await fcl::http::api::remote<cache_api>(client);
```

## Public Modules

- `fcl.http.api.parameters` — HTTP request DTO wrapper parameters and special
  response types that remain in namespace `fcl::http`.
- `fcl.http.api.mapping` — route metadata, `traits<T>`, route template parsing
  and rendering metadata.
- `fcl.http.api.binding` — `binding_builder`, `binding_plan`, `binding()` and
  server-side mount.
- `fcl.http.api.client_request` — typed proxy request construction internals.
- `fcl.http.api.client_response` — typed proxy response materialization and
  bounded error decode internals.
- `fcl.http.api.proxy` — `remote<T>()` and remote invoker glue.

Macro header: `<fcl/http_api/macros.hpp>`.

Target/component: `fcl_http_api` / `http_api`.

Dependencies: `fcl_http`, `fcl_api`, `fcl_json`, `fcl_schema`.

`fcl_http_api` may depend on `fcl_http`; `fcl_http` must not depend on
`fcl_http_api`. WebSocket, QUIC and P2P API builders are intentionally unchanged
in this layer split.
