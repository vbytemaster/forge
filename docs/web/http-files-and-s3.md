# HTTP Files And Object-Gateway Readiness

This note records the `forge_http` file/upload direction after the FastAPI-style
typed binding work. S3-compatible APIs are used only as a downstream readiness
driver: FORGE owns generic HTTP mechanics, while an application owns object
storage semantics, credentials, authorization, billing and gateway-specific
error shapes.

## Current HTTP Surface

`forge_http` owns:

- async router, route handlers and middleware;
- server, connection and client mechanics over `forge_asio`;
- request target parsing and base URL rendering;
- WebSocket upgrade routing;
- `FORGE_HTTP_API(...)` presentation metadata on top of `FORGE_API(...)`;
- JSON request/response DTOs for ordinary typed HTTP methods;
- path, query and header binding into described request DTOs;
- HTTP-only typed fields such as `header<T>`, `form_field<T>`,
  `body_stream`, `body_bytes` and `upload_file`;
- response wrappers such as `file_response`, `stream_response`,
  `bytes_response` and `empty_response`;
- stream routes that let handlers process request bodies chunk by chunk;
- bounded header/body reads, read timeouts and idle timeouts;
- file/range helpers for `HEAD`, `Range`, `206`, `416`, `ETag` and
  `Last-Modified` behavior.

The official HTTP server plugin is parked until the `forge_http` typed binding
surface is stable. This document describes the library substrate only.

## Binding Model

The intended application shape is one C++ API class per HTTP API surface:

```cpp
class object_api : public forge::api::contract<object_api> {
 public:
   virtual boost::asio::awaitable<put_response>
   put_object(put_request request) = 0;

   virtual boost::asio::awaitable<forge::http::file_response>
   get_object(get_request request) = 0;
};

FORGE_API(
   object_api,
   FORGE_API_CONTRACT("object", 1, 0),
   FORGE_API_METHOD_TYPED(put_object, put_request, put_response),
   FORGE_API_METHOD_TYPED(get_object, get_request, forge::http::file_response))

FORGE_HTTP_API(
   object_api,
   FORGE_HTTP_PUT(put_object, "/objects/:collection/:key", created,
      FORGE_HTTP_BODY_STREAM(body),
      FORGE_HTTP_HEADER(content_type, "Content-Type")),
   FORGE_HTTP_GET(get_object, "/objects/:collection/:key"))
```

`FORGE_API(...)` remains the contract metadata source. `FORGE_HTTP_API(...)` is only
the HTTP presentation: method, path, status and HTTP field mapping.

Ordinary DTO request/response bodies use JSON. HTTP-specific transfer mechanics
use explicit FORGE-owned special types rather than leaking Boost.Beast parser,
serializer or body templates through public modules.

## Object-Gateway Readiness

An object-storage gateway built above FORGE needs HTTP mechanics such as:

- method coverage for `GET`, `PUT`, `HEAD`, `DELETE`, `POST` and `OPTIONS`;
- stable path and query preservation;
- large streaming upload;
- streaming or file-backed download;
- range reads;
- digest and metadata headers as caller-visible fields;
- canonical header/query helpers where they are generic HTTP mechanics;
- request limits, timeouts, cancellation and cleanup;
- middleware hooks before body consumption.

FORGE does not own:

- bucket, object, tenant or account models;
- multipart object workflow;
- credential lookup, request signing policy or authorization;
- object metadata/versioning semantics;
- gateway-specific XML error payloads;
- mapping to any product storage or control-plane state.

FORGE may add generic HTTP canonicalization helpers if they are useful outside a
single object gateway. Product policy and compatibility-specific semantics stay
above FORGE.

## Remaining Gaps

The current slice proves the server-side typed binding model for JSON DTOs,
streaming request bodies and file responses. Remaining HTTP-library work should
stay generic:

- complete route option metadata instead of macro placeholders for custom
  header/form names;
- typed HTTP client streaming request writer and response reader;
- richer multipart/form-data diagnostics;
- `Expect: 100-continue`;
- chunked transfer tests for unknown-length streams;
- stronger canonical header/query helpers;
- optional MIME-type helper with conservative fallback;
- more file-response security coverage for symlink policy and hidden files.

None of those gaps require an HTTP server plugin or product-specific route API.

## Non-Goals

- Do not add object-storage product vocabulary to FORGE runtime APIs.
- Do not add credential stores, signing policy, billing or tenant authorization.
- Do not expose Boost.Beast parser/serializer/body templates as public FORGE API.
- Do not add raw route mutation as the application-facing pattern.
- Do not buffer large files in memory as a production transfer path.
