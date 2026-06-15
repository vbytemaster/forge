# HTTP Files And S3 Readiness

This note records the current `fcl_http` state after the HTTP API foundation and
`http_server` plugin work, then defines what is still missing for
production-grade file upload/download. S3 is used here as a downstream
readiness driver: FCL should make it possible for Storlane to build an
S3-compatible gateway cleanly, but FCL does not implement that gateway.

## Current HTTP Surface

`fcl_http` currently owns:

- async `router`, route handlers and middleware;
- `server` and `client` over `fcl_asio`;
- `base_url` and request target parsing;
- WebSocket upgrade routing;
- typed `FCL_HTTP_API(...)` mapping over `FCL_API(...)`;
- path/query extraction into described request DTOs;
- strict `fcl.raw` request/response bodies for typed HTTP APIs;
- bounded request/header reads through server config;
- read and idle timeouts;
- per-connection HTTP client serialization and retry rules.

The official `fcl.plugins.http_server` plugin now owns application lifecycle for
one HTTP server and exposes local-only `publisher` and `middleware` APIs. It is
the right composition point for application plugins that publish typed HTTP
APIs. It intentionally does not expose diagnostics, status endpoints or raw
route mutation.

## Current Body Model

The public HTTP aliases are currently string-body based:

```cpp
using request = boost::beast::http::request<string_body>;
using response = boost::beast::http::response<string_body>;
```

The server reads each request through `request_parser<string_body>` and releases
a full in-memory request before routing. The client writes a full request and
reads a full in-memory response. `FCL_HTTP_API` packs/unpacks typed bodies as
full `fcl.raw` byte strings.

This is suitable for:

- control-plane APIs;
- JSON or raw DTO requests;
- small binary payloads;
- bounded tests and admin endpoints;
- typed API-over-HTTP request/response flows.

It is not sufficient for production file transfer. A file upload or download is
currently a full-memory operation, limited only by `max-request-body-bytes`.
That is safe as a guardrail, but it is not a streaming file API.

## Gaps For File Upload And Download

FCL still needs HTTP-native file primitives before it should be used for serious
file serving or uploads.

Required upload capabilities:

- request header read before body consumption;
- streaming request body reader with backpressure;
- bounded in-memory chunks, not whole-body buffering;
- optional temp-file spool for large uploads;
- upload cancellation cleanup;
- incremental digest/hash support at the consumer layer;
- clear typed errors for body limit, malformed body, client disconnect and
  timeout;
- optional multipart form-data parsing for browser-compatible uploads.

Required download capabilities:

- streaming response writer;
- file-backed responses without loading a file into memory;
- `Content-Length` when known;
- chunked transfer when length is not known;
- client disconnect/cancel handling;
- safe static file serving from an explicit root;
- content type detection or caller-provided MIME type;
- `Content-Disposition` support for attachment downloads.

Required HTTP metadata capabilities:

- `HEAD`;
- `Range` and `Content-Range`;
- `206 Partial Content`;
- `416 Range Not Satisfiable`;
- `ETag`;
- `Last-Modified`;
- `If-None-Match`;
- `If-Modified-Since`;
- `Cache-Control`;
- `Accept-Ranges`;
- stable canonical header helpers.

Required security/path capabilities:

- root-jail path normalization;
- directory traversal protection;
- symlink policy;
- hidden-file policy controlled by the caller;
- maximum path depth/segment length;
- no implicit directory listing unless a caller explicitly provides it.

## Boost.Beast Capabilities Not Yet Used

The most important Beast functionality not yet used by FCL is:

- `http::file_body` / `basic_file_body` for file-backed responses;
- `http::buffer_body` for chunk-by-chunk streaming;
- custom Body models for FCL-owned stream readers/writers;
- `http::serializer` for incremental response writes;
- `request_parser` staged operation:
  - `async_read_header`;
  - incremental body reads after headers are validated;
- parser body/header controls beyond the current full `string_body` path;
- chunked transfer handling;
- EOF and keep-alive mechanics for streaming bodies;
- typed handling of `Expect: 100-continue`.

FCL should wrap these as FCL-owned concepts instead of leaking Beast parser,
serializer or body template types through public module interfaces.

## FastAPI-Level Expectations

For a FastAPI-like developer experience, FCL should support both typed APIs and
HTTP-native file endpoints without forcing everything into `FCL_API`.

Typed API path:

- `FCL_HTTP_API` remains the right path for RPC-like DTO calls;
- request and response are fully typed;
- body remains strict `fcl.raw` until a separate JSON DTO block is planned;
- route/query fields map into request DTOs.

File path:

- file upload/download should use explicit HTTP-native route primitives;
- a route handler should receive a body reader or upload object;
- a route handler should return a file/stream response object;
- middleware should be able to inspect headers before body consumption;
- limits and timeouts should apply before and during streaming;
- file routes should be publishable through the `http_server` plugin later,
  but not through `FCL_HTTP_API`.

This split avoids overloading typed API contracts with transport-specific file
semantics and keeps file transfer honest about HTTP behavior.

## S3 Readiness Driver

S3 means an object-storage HTTP API compatible with the common Amazon S3 request
shape. In this roadmap it is a requirements driver, not an FCL feature.

FCL owns the HTTP mechanics needed by such a gateway:

- streaming uploads and downloads;
- file/range/static-file response mechanics;
- route/path/query preservation;
- header normalization helpers where they are generic HTTP mechanics;
- middleware and plugin publication lifecycle;
- request limits, timeouts, cancellation and cleanup.

Storlane owns the S3 gateway:

- S3 route matrix and operation semantics;
- bucket and object model;
- multipart upload workflow;
- SigV4 credential lookup and authorization;
- object metadata, versioning and consistency semantics;
- XML S3 error response shape;
- mapping to Storlane storage and control-plane state.

Core HTTP features needed by a downstream S3 gateway:

- method coverage:
  - `GET`;
  - `PUT`;
  - `HEAD`;
  - `DELETE`;
  - `POST`;
  - `OPTIONS`;
- object paths with bucket/key extraction;
- query parameter preservation for subresources such as `uploads`, `partNumber`,
  `uploadId`, `versionId`, `list-type`, `prefix`, `delimiter`, `continuation-token`;
- canonical target and query rendering without lossy reordering;
- large streaming `PUT Object`;
- streaming `GET Object`;
- `Range` reads;
- multipart upload flow:
  - initiate multipart upload;
  - upload part;
  - complete multipart upload;
  - abort multipart upload;
  - list parts;
- checksums and digest headers:
  - `Content-MD5`;
  - `x-amz-checksum-*`;
  - caller-owned digest validation hooks;
- metadata headers:
  - `ETag`;
  - `Last-Modified`;
  - `Content-Type`;
  - `Content-Length`;
  - `x-amz-meta-*`;
  - storage-class and versioning headers as opaque caller-visible fields;
- error responses with stable XML-compatible S3 error shape;
- request id and host id response headers generated by the service layer.

Authentication support needed by a downstream S3 gateway:

- canonical request construction helpers;
- canonical header normalization;
- signed query handling;
- payload hash modes including unsigned payload where a product permits it;
- date/header validation hooks;
- no built-in credential store in `fcl_http`;
- no product authorization policy in `fcl_http`;
- no S3-specific signing policy in FCL.

FCL may provide generic HTTP canonicalization primitives if they are useful
outside S3 as well. Actual SigV4 signing, key lookup, tenant policy, bucket
ownership, object permissions and audit rules belong above FCL.

## Proposed FCL Additions

### 1. HTTP Streaming Primitives

Add FCL-owned public types, not Beast template leakage:

- `body_reader`;
- `body_writer`;
- `stream_request`;
- `stream_response`;
- `file_response`;
- `range_request`;
- `range_response`;
- `upload_limits`;
- `download_options`.

The server should be able to route after headers are read and before the body is
fully consumed. Middleware should be allowed to short-circuit before body
streaming starts.

### 2. File Response And Static File Layer

Add a focused file-serving layer over the streaming primitives:

- safe file root;
- path normalization;
- MIME/content-type hooks;
- `HEAD`;
- `Range`;
- `ETag` and `Last-Modified` helpers;
- cache headers;
- attachment disposition.

This layer should be usable directly from `fcl_http` and later publishable
through `http_server` without raw router mutation.

### 3. Upload Spool Layer

Add upload handling that can choose memory or file spool based on size:

- small upload in memory;
- large upload in temp file;
- bounded chunk size;
- cleanup on cancel/error;
- caller-owned digest calculation hooks;
- optional multipart/form-data parser.

This should be separate from S3 multipart upload; the names are similar, but the
semantics are different. HTTP multipart form-data is browser upload encoding.
S3 multipart upload is an object-storage protocol workflow.

### 4. HTTP Publisher API Extensions

After the library primitives exist, extend `fcl.plugins.http_server` with
high-level publisher APIs for file/stream routes:

- publish typed HTTP APIs through current `publisher`;
- publish file/static routes through a separate focused API;
- publish upload routes through a separate focused API;
- keep diagnostics/status/raw router mutation out of the plugin.

The plugin should remain an application lifecycle owner, not the place where
file semantics are implemented.

### 5. Downstream S3 Gateway Plan

After the HTTP file substrate is solid, create a separate Storlane plan for the
S3 gateway. That plan should consume FCL streaming/file/upload primitives
without requiring:

- full-memory file transfer;
- raw route mutation through `http_server`;
- product vocabulary in FCL runtime APIs;
- S3-specific helpers in `fcl_http`.

The Storlane gateway can then define the S3 route matrix, SigV4 integration,
bucket/object semantics, XML errors and storage/control-plane mapping at the
product layer where those decisions belong.

## Non-Goals

- Do not turn `FCL_HTTP_API` into a file transfer abstraction.
- Do not add product storage, bucket policy, billing or tenant semantics to FCL.
- Do not add S3 route matrices, SigV4 credential lookup or S3 XML errors to FCL.
- Do not expose Beast parser/serializer/body templates as public FCL API.
- Do not add raw route mutation APIs to `http_server`.
- Do not make S3 support depend on P2P, plugins or Storlane-specific code.
- Do not buffer large files in memory as a production path.

## Suggested Iteration Order

1. Add streaming request/response primitives to `fcl_http`.
2. Add file response, range and static-file helpers.
3. Add upload body reader/spool and multipart form-data parser.
4. Extend `http_server` with focused file/upload publisher APIs.
5. Write the downstream Storlane S3 gateway plan against those primitives.

This order keeps the core HTTP substrate clean and avoids building downstream
S3 behavior on top of full-memory request/response shortcuts.
