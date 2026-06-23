# HTTP Server Plugin

`forge::plugins::http::server` owns one configured `forge_http` server and exposes a
local-only API for application plugins to publish typed HTTP APIs and middleware
before startup.

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

It does not expose raw route verbs, raw `forge::http::router`, file/upload
publishers, diagnostics/status endpoints, auth policy, TLS policy, CORS policy
or product-specific behavior.

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

## Object API Example

This example is intentionally object-storage shaped: it demonstrates native
HTTP paths, streaming upload bodies, file responses and endpoint metadata. It is
not an S3 implementation and does not add S3 signing, bucket policy, versioning
or storage semantics to FORGE.

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

struct put_object_request : forge::http::endpoint_request {
   std::string bucket;
   std::string key;
   forge::http::body_stream body;
};

struct get_object_request : forge::http::endpoint_request {
   std::string bucket;
   std::string key;
};

struct put_object_response {
   std::string etag;
};

BOOST_DESCRIBE_STRUCT(put_object_request, (), (bucket, key, body))
BOOST_DESCRIBE_STRUCT(get_object_request, (), (bucket, key))
BOOST_DESCRIBE_STRUCT(put_object_response, (), (etag))

class object_api : public forge::api::contract<object_api> {
 public:
   virtual boost::asio::awaitable<put_object_response>
   put_object(put_object_request request) = 0;

   virtual boost::asio::awaitable<forge::http::file_response>
   get_object(get_object_request request) = 0;

   virtual boost::asio::awaitable<forge::http::empty_response>
   delete_object(get_object_request request) = 0;
};

FORGE_API(object_api,
   FORGE_API_CONTRACT("example.object", 1, 0),
   FORGE_API_METHOD(put_object),
   FORGE_API_METHOD_TYPED(get_object, get_object_request, forge::http::file_response),
   FORGE_API_METHOD_TYPED(delete_object, get_object_request, forge::http::empty_response))

FORGE_HTTP_API(object_api,
   FORGE_HTTP_PUT(put_object, "/objects/:bucket/:key", created),
   FORGE_HTTP_GET(get_object, "/objects/:bucket/:key", ok, FORGE_HTTP_RESPONSE_FILE),
   FORGE_HTTP_DELETE(delete_object, "/objects/:bucket/:key", no_content))
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

      co_await http->publish<object_api>(
         forge::plugins::http::server::publish_options{.base_path = "/api/v1"});
   }
};
```

Register the infrastructure plugin once:

```cpp
registry.register_plugin(forge::plugins::http::server::descriptor());
```
