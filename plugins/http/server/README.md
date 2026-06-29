# HTTP Server Plugin

`forge::plugins::http::server` owns one configured `forge_http` server and exposes a
local-only API for application plugins to publish typed HTTP APIs and middleware
before startup.

## When To Use

- A `forge_app` daemon needs one shared HTTP server configured through the
  plugin config tree.
- Several application plugins need to contribute typed HTTP APIs or middleware
  before server startup.
- You want native HTTP route/path/status semantics without each product plugin
  owning sockets and lifecycle.

## When Not To Use

- Do not use this plugin for raw socket ownership, TLS policy or transport
  experiments. Use `forge_http` directly for low-level HTTP mechanics.
- Do not put product authorization, account policy, storage policy or protocol
  vocabulary into this plugin.
- Do not publish routes after the server has entered startup; contribution
  windows are lifecycle-bound.

## Identity

- Target: `forge_plugins_http_server`
- Package component: `plugins_http_server`
- Plugin id: `forge.plugins.http.server`
- Main API id: `forge.plugins.http.server`
- Config section: `plugins.http.server`
- Public modules:
  - `forge.plugins.http.server.plugin`
  - `forge.plugins.http.server.api`
  - `forge.plugins.http.server.middleware`
  - `forge.plugins.http.server.types`
  - `forge.plugins.http.server.exceptions`

## What It Provides

- Starts and stops one HTTP server through the `forge_app` lifecycle.
- Applies schema-driven server config: bind address, port, base path, body/header
  limits and timeouts.
- Accepts typed `FORGE_HTTP_API` publications through `publish<Interface>()`.
- Accepts plugin-owned middleware descriptors through
  `forge::plugins::http::server::middleware_descriptor`.

It does not expose raw route verbs, raw `forge::http::router`,
diagnostics/status endpoints, auth policy, TLS policy, CORS policy or
product-specific behavior.

## Dependencies

- `forge_app`
- `forge_api`
- `forge_http`
- `forge_http_api`
- `forge_config`
- `forge_schema`
- Boost.Asio

## Config

```yaml
plugins:
   http:
      server:
         bind-address: 127.0.0.1
         port: 8080
         api-base-path: /api/v1
         max-request-body-bytes: 16777216
         max-header-bytes: 65536
         read-timeout-ms: 30000
         idle-timeout-ms: 120000
```

`api-base-path` is the default base path for published typed APIs. A
publication can override it with `publish_options::base_path`.

## Examples

### Catalog API

This example demonstrates typed HTTP route publication, a native path parameter,
an XML request/response route and middleware. It intentionally uses neutral
catalog vocabulary; protocol-specific route matrices and policy belong in the
consumer.

```cpp
#include <boost/describe.hpp>
#include <forge/api/macros.hpp>
#include <forge/http_api/macros.hpp>

import forge.api.binding;
import forge.app.plugin;
import forge.http.file;
import forge.http.stream;
import forge.http.types;
import forge.plugins.http.server.api;
import forge.plugins.http.server.middleware;
import forge.plugins.http.server.plugin;

struct item_request : forge::http::endpoint_request {
   std::string id;
};

struct update_item_request : forge::http::endpoint_request {
   std::string id;
   std::string title;
};

struct item_response {
   std::string id;
   std::string title;
};

BOOST_DESCRIBE_STRUCT(item_request, (), (id))
BOOST_DESCRIBE_STRUCT(update_item_request, (), (id, title))
BOOST_DESCRIBE_STRUCT(item_response, (), (id, title))

class catalog_api : public forge::api::contract<catalog_api> {
 public:
   virtual boost::asio::awaitable<item_response>
   read_item(item_request request) = 0;

   virtual boost::asio::awaitable<item_response>
   update_item(update_item_request request) = 0;
};

FORGE_API(catalog_api,
   FORGE_API_CONTRACT("example.catalog", 1, 0),
   FORGE_API_METHOD(read_item),
   FORGE_API_METHOD(update_item))

FORGE_HTTP_API(catalog_api,
   FORGE_HTTP_GET(read_item, "/items/:id", ok),
   FORGE_HTTP_PUT(update_item, "/items/:id", ok,
      FORGE_HTTP_REQUEST_BODY(xml),
      FORGE_HTTP_RESPONSE_BODY(xml),
      FORGE_HTTP_ERROR_BODY(xml)))
```

The application plugin only contributes the typed API and middleware. The HTTP
server plugin applies the configured server lifecycle and mounts the route
mapping under the configured base path.

```cpp
class object_http_plugin final : public forge::app::plugin {
 public:
   [[nodiscard]] forge::app::plugin_id id() const override {
      return {.value = "object.http"};
   }

   [[nodiscard]] std::string version() const override {
      return "1.0.0";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto http = context.apis().get<forge::plugins::http::server::api>(
         {.id = {"forge.plugins.http.server"}, .major = 1});

      co_await http->use(forge::plugins::http::server::middleware_descriptor{
         .id = "trace",
         .phase = forge::plugins::http::server::middleware_phase::before_handler,
         .order = 10,
         .path_prefix = "/api",
         .handler = [](const forge::plugins::http::server::middleware_request&,
                       forge::plugins::http::server::middleware_next next)
            -> boost::asio::awaitable<forge::plugins::http::server::middleware_response> {
            auto response = co_await next();
            response.set_header("X-Trace", "enabled");
            co_return response;
         },
      });

      co_await http->publish<catalog_api>(
         forge::plugins::http::server::publish_options{.base_path = "/api/v1"});
   }
};
```

Register the infrastructure plugin once:

```cpp
registry.register_plugin(forge::plugins::http::server::descriptor());
```

## Security And Boundaries

- The plugin is not an authority boundary. Authentication, authorization,
  tenancy and rate policy belong to middleware or consuming product plugins.
- Body/header limits and timeouts are config-owned and enforced by `forge_http`.
- Middleware should avoid logging raw headers, query strings or request bodies
  before redaction.
- Native file/stream responses are route-level escape hatches; they do not go
  through JSON/XML DTO codecs.

## Common Mistakes

- Publishing an API after startup. Publish from application plugin
  initialization.
- Adding product-specific route helpers to this plugin instead of using typed
  `FORGE_HTTP_API` mappings.
- Treating `api-base-path` as a security boundary. It is only route mounting.
- Reimplementing HTTP server lifecycle in each application plugin instead of
  sharing this plugin.

## Tests

- `test_forge_plugins`
- `test_forge_http_websocket`
- `test_forge_api`
