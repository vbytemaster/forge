# FCL HTTP Files Donor Note

This note records donor patterns for the next `fcl_http` file, upload and
streaming work. The goal is FastAPI-level ergonomics with C++/Boost.Asio
runtime boundaries, not a Python API clone and not an S3 implementation inside
FCL.

## Scope

FCL owns HTTP mechanics:

- streaming request and response bodies;
- file and range responses;
- upload spooling and multipart form-data parsing;
- request limits, timeouts, cancellation and cleanup;
- typed HTTP route binding over FCL-owned request/response primitives.

FCL does not own S3:

- no S3 route matrix;
- no bucket or object model;
- no S3 multipart upload lifecycle;
- no SigV4 credential lookup or authorization;
- no S3 XML error shape;
- no Storlane storage/control-plane mapping.

S3 is a downstream compatibility target for Storlane and a quality driver for
FCL HTTP primitives. It is not an FCL feature.

## Donors Inspected

- FastAPI `UploadFile`: <https://fastapi.tiangolo.com/reference/uploadfile/>
- FastAPI file upload tutorial:
  <https://fastapi.tiangolo.com/tutorial/request-files/>
- FastAPI custom responses:
  <https://fastapi.tiangolo.com/advanced/custom-response/>
- Starlette requests/files: <https://starlette.dev/requests/>
- Starlette responses: <https://starlette.dev/responses/>
- Boost.Beast `file_body`:
  <https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__http__file_body.html>
- Boost.Beast `buffer_body`:
  <https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__http__buffer_body.html>
- Amazon S3 API reference:
  <https://docs.aws.amazon.com/AmazonS3/latest/API/Welcome.html>
- Amazon S3 multipart upload overview:
  <https://docs.aws.amazon.com/AmazonS3/latest/userguide/mpuoverview.html>

## Accepted Patterns

From FastAPI and Starlette:

- separate typed request/response APIs from HTTP-native file transfer;
- offer an upload abstraction that can expose metadata and file-like access;
- support multipart form-data limits for max files, fields and part size;
- offer streaming/file responses as first-class response shapes;
- keep route handlers ergonomic while preserving caller-owned application
  policy.

From Boost.Beast:

- use staged header/body parsing so middleware can reject before body read;
- use incremental body mechanics for large uploads and downloads;
- use file-backed response mechanics for large static or object-like payloads;
- use serializers for chunked/incremental response writes;
- keep parser, serializer and Body template details behind FCL-owned public
  types.

From S3 docs as a requirements driver:

- support large streaming `PUT` and `GET`;
- preserve method, path, query and headers without lossy normalization;
- support `HEAD`, `Range`, `ETag`, `Last-Modified`, checksums and metadata
  headers as generic HTTP/file primitives;
- allow downstream code to implement multipart upload and request-signing policy
  without full-memory transfer or direct router mutation.

## Rejected Patterns

- exposing Boost.Beast parser, serializer, `file_body` or `buffer_body` types
  in public FCL modules;
- implementing S3 routes, SigV4 credential lookup, bucket/object state or S3
  XML errors in FCL;
- treating file transfer as ordinary in-memory JSON DTO payloads;
- exposing direct router mutation as the public application pattern;
- buffering large files in memory as the production path;
- adding Storlane-specific names or product policy to `fcl_http`.

## Test Expectations

Future implementation blocks should prove behavior with large-enough payloads
to distinguish streaming from string buffering:

- middleware can short-circuit after headers and before body read;
- limits and timeouts fire while streaming;
- file responses do not allocate the whole file as response body;
- range responses return exact byte spans and correct status codes;
- upload spool files are cleaned on cancel/error;
- typed HTTP bindings expose file/upload primitives without direct router access.
