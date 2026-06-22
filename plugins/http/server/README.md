# HTTP Server Plugin

`fcl::plugins::http::server` owns one configured `fcl_http` server and exposes a
local-only API for application plugins to publish typed HTTP APIs and middleware
before startup.

## Identity

- Target: `fcl_plugins_http_server`
- Package component: `plugins_http_server`
- Plugin id: `fcl.plugins.http.server`
- Main API id: `fcl.plugins.http.server`
- Config section: `plugins.http.server`
- Public modules:
  - `fcl.plugins.http.server.plugin`
  - `fcl.plugins.http.server.api`
  - `fcl.plugins.http.server.middleware`
  - `fcl.plugins.http.server.types`
  - `fcl.plugins.http.server.exceptions`

## What It Provides

- Starts and stops one HTTP server through the `fcl_app` lifecycle.
- Applies schema-driven server config: bind address, port, base path, body/header
  limits and timeouts.
- Accepts typed `FCL_HTTP_API` publications through `publish<Interface>()`.
- Accepts plugin-owned middleware descriptors through
  `fcl::plugins::http::server::middleware_descriptor`.

It does not expose raw route verbs, raw `fcl::http::router`, file/upload
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
or storage semantics to FCL.

```cpp
#include <boost/describe.hpp>
#include <fcl/api/macros.hpp>
#include <fcl/http_api/macros.hpp>

import fcl.api.binding;
import fcl.app.plugin;
import fcl.http.file;
import fcl.http.stream;
import fcl.http.types;
import fcl.plugins.http.server.api;
import fcl.plugins.http.server.middleware;
import fcl.plugins.http.server.plugin;

struct put_object_request : fcl::http::endpoint_request {
   std::string bucket;
   std::string key;
   fcl::http::body_stream body;
};

struct get_object_request : fcl::http::endpoint_request {
   std::string bucket;
   std::string key;
};

struct put_object_response {
   std::string etag;
};

BOOST_DESCRIBE_STRUCT(put_object_request, (), (bucket, key, body))
BOOST_DESCRIBE_STRUCT(get_object_request, (), (bucket, key))
BOOST_DESCRIBE_STRUCT(put_object_response, (), (etag))

class object_api : public fcl::api::contract<object_api> {
 public:
   virtual boost::asio::awaitable<put_object_response>
   put_object(put_object_request request) = 0;

   virtual boost::asio::awaitable<fcl::http::file_response>
   get_object(get_object_request request) = 0;

   virtual boost::asio::awaitable<fcl::http::empty_response>
   delete_object(get_object_request request) = 0;
};

FCL_API(object_api,
   FCL_API_CONTRACT("example.object", 1, 0),
   FCL_API_METHOD(put_object),
   FCL_API_METHOD_TYPED(get_object, get_object_request, fcl::http::file_response),
   FCL_API_METHOD_TYPED(delete_object, get_object_request, fcl::http::empty_response))

FCL_HTTP_API(object_api,
   FCL_HTTP_PUT(put_object, "/objects/:bucket/:key", created),
   FCL_HTTP_GET(get_object, "/objects/:bucket/:key", ok, FCL_HTTP_RESPONSE_FILE),
   FCL_HTTP_DELETE(delete_object, "/objects/:bucket/:key", no_content))
```

The application plugin only contributes the typed API and middleware. The HTTP
server plugin applies the configured server lifecycle and mounts the route
mapping under the configured base path.

```cpp
class object_http_plugin final : public fcl::app::plugin {
 public:
   [[nodiscard]] fcl::app::plugin_id id() const override {
      return {.value = "object.http"};
   }

   [[nodiscard]] std::string version() const override {
      return "1.0.0";
   }

   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override {
      auto http = context.apis().get<fcl::plugins::http::server::api>(
         {.id = {"fcl.plugins.http.server"}, .major = 1});

      co_await http->use(fcl::plugins::http::server::middleware_descriptor{
         .id = "trace",
         .phase = fcl::plugins::http::server::middleware_phase::before_handler,
         .order = 10,
         .path_prefix = "/api",
         .handler = [](const fcl::plugins::http::server::middleware_request&,
                       fcl::plugins::http::server::middleware_next next)
            -> boost::asio::awaitable<fcl::plugins::http::server::middleware_response> {
            auto response = co_await next();
            response.set_header("X-Trace", "enabled");
            co_return response;
         },
      });

      co_await http->publish<object_api>(
         fcl::plugins::http::server::publish_options{.base_path = "/api/v1"});
   }
};
```

Register the infrastructure plugin once:

```cpp
registry.register_plugin(fcl::plugins::http::server::descriptor());
```
