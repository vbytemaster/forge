# Forge XML And HTTP API Codec Plan

This note fixes the implementation order for XML (Extensible Markup Language)
support and S3-ready HTTP API binding. The first block is the neutral
`forge_xml` library. The second block extends `forge_http_api` from a JSON-only
body/error binding into a multi-codec HTTP API layer. Downstream S3 gateways
must use `FORGE_API(...)` plus `FORGE_HTTP_API(...)`; bypassing the API layer
with manual router handlers is not the product path.

## Block 1: `forge_xml`

Add a standalone library:

- target: `forge_xml`;
- namespace: `forge::xml`;
- public module: `forge.xml`;
- public path: `libraries/xml/include/forge/xml/xml.cppm`;
- library guide: `libraries/xml/README.md`;
- package component: `xml`.

Recommended backend is `pugixml`, kept private to implementation files. Public
modules must not expose backend namespace types. Build the backend without XPath
by default and keep XML parser/writer options behind Forge-owned DTOs.

Public API shape follows `forge_json` and `forge_yaml`:

- `read_value` / `write_value` for generic `forge::variant`;
- `read_document` / `write_document` for `forge::config::document`;
- `read<T>` / `write<T>` for Boost.Describe and `forge_schema` described types;
- `load_*` / `save_*` for filesystem paths only if JSON/YAML parity requires it;
- `read_options` with source name, max bytes, max depth and unknown field policy;
- `write_options` with pretty output, max bytes and deadline.

Security and compatibility defaults:

- reject DTD (Document Type Definition), external entities, processing
  instructions and comments unless a future generic Forge use case explicitly
  needs them;
- enforce bounded input size, node depth, text length, attribute count and
  output size;
- produce `forge::schema::diagnostic` entries with paths and codes;
- serialize schema-secret/redacted values only after caller-selected redaction,
  matching JSON/YAML behavior;
- support XML element text, attributes, namespaces and repeated child elements
  enough for object-storage style APIs without embedding product vocabulary.

## Block 2: HTTP API Multi-Codec

After `forge_xml` lands, extend `forge_http_api` rather than writing product
routes directly against `forge_http::router`.

Changes:

- keep JSON as the default request, response and error codec;
- add route-level request body codec, response body codec and error codec;
- support XML request/response DTO bodies through `forge::xml::read/write`;
- support `application/xml`, `text/xml` and structured `+xml` content types;
- keep positional HTTP arguments bounded to simple path/query use; XML request
  bodies stay DTO/envelope based;
- add XML error projection hooks without putting S3-specific error DTOs in
  Forge;
- preserve bytes, stream, file and empty response special types without forcing
  them through XML/JSON codecs;
- keep all backend parser types out of public `.cppm` files.

The S3-compatible gateway then declares its operations through API contracts and
HTTP presentation metadata. It can choose XML body/error profiles while still
using the same FastAPI-style binding system as JSON endpoints.

## HTTP Substrate Follow-Up

`forge_http` remains the transport substrate. It should receive only generic
mechanics needed by the API layer and downstream object APIs:

- `Expect: 100-continue` support that lets middleware/handlers reject by headers
  before the body is consumed;
- stricter content negotiation helpers for `Accept` and `Content-Type`;
- bounded XML error/body helpers if they are generic and do not depend on S3;
- stronger tests for chunked transfer, stream limits and response body framing.

Do not move S3 route classification, SigV4 signing, bucket/object models,
storage policy or product error vocabulary into Forge.

## Tests And Acceptance

`forge_xml` tests:

- generic value/document read-write roundtrip;
- typed Boost.Describe + `forge_schema` read/write;
- attributes, namespaces, repeated elements and text nodes;
- malformed XML diagnostics with source paths;
- DTD/entity/processing-instruction rejection;
- depth, text, attribute and output size limits;
- package consumer smoke for `Forge::forge_xml`.

`forge_http_api` tests:

- JSON default behavior remains compatible;
- XML request/response body roundtrip through typed API client and server;
- XML error profile maps typed API errors without JSON fallback;
- unsupported media type and unacceptable response type fail typed;
- stream/file/bytes/empty response modes bypass DTO codecs correctly;
- S3-shaped sample bodies can be represented without manual router handlers.

Validation:

```bash
cmake --build build/forge-debug -j 1 \
  --target test_forge_xml test_forge_http_websocket test_forge_api \
           test_forge_package_xml_component

ctest --test-dir build/forge-debug --output-on-failure \
  -R "^(test_forge_xml|test_forge_http_websocket|test_forge_api|test_forge_package_xml_component)$" \
  --timeout 300

rg "$FORGE_XML_BACKEND_LEAK_PATTERN" \
  libraries/xml/include libraries/http_api/include -g "*.cppm"

git diff --check
```

## Assumptions

- `pugixml` is the default backend unless size/package benchmarking disproves
  it before implementation.
- `forge_xml` is neutral infrastructure, not an S3 library.
- `forge_http_api` is the mandatory HTTP application binding layer for S3-style
  APIs; manual router bypass is allowed only for low-level substrate tests and
  examples.
- S3-specific XML DTOs, error codes, SigV4, bucket semantics and object
  workflows belong to downstream products.
