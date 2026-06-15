# HTTP Files And Object-Gateway Readiness

This note records the `fcl_http` file/upload direction after the FastAPI-style
typed binding work. S3-compatible APIs are used only as a downstream readiness
driver: FCL owns generic HTTP mechanics, while an application owns object
storage semantics, credentials, authorization, billing and gateway-specific
error shapes.

## Current HTTP Surface

`fcl_http` owns:

- async router, route handlers and middleware;
- server, connection and client mechanics over `fcl_asio`;
- request target parsing and base URL rendering;
- WebSocket upgrade routing;
- `FCL_HTTP_API(...)` presentation metadata on top of `FCL_API(...)`;
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

The official HTTP server plugin is parked until the `fcl_http` typed binding
surface is stable. This document describes the library substrate only.

## Binding Model

The intended application shape is one C++ API class per HTTP API surface:

```cpp
class object_api : public fcl::api::contract<object_api> {
 public:
   virtual boost::asio::awaitable<put_response>
   put_object(put_request request) = 0;

   virtual boost::asio::awaitable<fcl::http::file_response>
   get_object(get_request request) = 0;
};

FCL_API(
   object_api,
   FCL_API_CONTRACT("object", 1, 0),
   FCL_API_METHOD_TYPED(put_object, put_request, put_response),
   FCL_API_METHOD_TYPED(get_object, get_request, fcl::http::file_response))

FCL_HTTP_API(
   object_api,
   FCL_HTTP_PUT(put_object, "/objects/:collection/:key", created,
      FCL_HTTP_BODY_STREAM(body),
      FCL_HTTP_HEADER(content_type, "Content-Type")),
   FCL_HTTP_GET(get_object, "/objects/:collection/:key"))
```

`FCL_API(...)` remains the contract metadata source. `FCL_HTTP_API(...)` is only
the HTTP presentation: method, path, status and HTTP field mapping.

Ordinary DTO request/response bodies use JSON. HTTP-specific transfer mechanics
use explicit FCL-owned special types rather than leaking Boost.Beast parser,
serializer or body templates through public modules.

## Object-Gateway Readiness

An object-storage gateway built above FCL needs HTTP mechanics such as:

- method coverage for `GET`, `PUT`, `HEAD`, `DELETE`, `POST` and `OPTIONS`;
- stable path and query preservation;
- large streaming upload;
- streaming or file-backed download;
- range reads;
- digest and metadata headers as caller-visible fields;
- canonical header/query helpers where they are generic HTTP mechanics;
- request limits, timeouts, cancellation and cleanup;
- middleware hooks before body consumption.

FCL does not own:

- bucket, object, tenant or account models;
- multipart object workflow;
- credential lookup, request signing policy or authorization;
- object metadata/versioning semantics;
- gateway-specific XML error payloads;
- mapping to any product storage or control-plane state.

FCL may add generic HTTP canonicalization helpers if they are useful outside a
single object gateway. Product policy and compatibility-specific semantics stay
above FCL.

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

- Do not add object-storage product vocabulary to FCL runtime APIs.
- Do not add credential stores, signing policy, billing or tenant authorization.
- Do not expose Boost.Beast parser/serializer/body templates as public FCL API.
- Do not add raw route mutation as the application-facing pattern.
- Do not buffer large files in memory as a production transfer path.
