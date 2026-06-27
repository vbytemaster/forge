# Forge XML And HTTP API Codec Donor Note

This donor note records the patterns accepted for the `forge_xml` library and
the follow-up `forge_http_api` multi-codec binding work.

## Donors Inspected

- pugixml: <https://pugixml.org/> and <https://pugixml.org/docs/manual.html>
- TinyXML-2: <https://github.com/leethomason/tinyxml2>
- Expat: <https://libexpat.github.io/>
- libxml2: <https://gitlab.gnome.org/GNOME/libxml2>
- FastAPI response and parameter binding:
  <https://fastapi.tiangolo.com/advanced/custom-response/> and
  <https://fastapi.tiangolo.com/reference/parameters/>
- Amazon S3 API reference:
  <https://docs.aws.amazon.com/AmazonS3/latest/API/Welcome.html>
- Amazon S3 `ListObjectsV2` and multipart XML bodies:
  <https://docs.aws.amazon.com/AmazonS3/latest/API/API_ListObjectsV2.html> and
  <https://docs.aws.amazon.com/AmazonS3/latest/API/API_CompleteMultipartUpload.html>
- Existing Forge JSON/YAML codec architecture:
  `docs/codecs/json-yaml-glaze.md`

## Accepted Patterns

From pugixml:

- lightweight DOM-style read/write backend;
- private backend dependency with Forge-owned public DTOs;
- optional XPath excluded from the default build;
- efficient construction of XML documents from described value objects.

From TinyXML-2:

- keep the public XML surface simple and learnable;
- support constructing documents from code, not only parsing.

From Expat:

- fail closed on untrusted input;
- keep streaming/security concerns visible through explicit limits.

From FastAPI:

- HTTP presentation metadata is separate from typed API contracts;
- content type and response class/profile are route presentation choices;
- users should not drop to raw router code for ordinary application endpoints.

From Amazon S3:

- XML request/response bodies are required for object gateway compatibility;
- success and error bodies can be XML even when HTTP status alone is not enough;
- large object transfer remains streaming/file based, not XML/JSON DTO based.

From existing Forge JSON/YAML:

- namespace-style `read<T>` / `write<T>` API;
- Boost.Describe plus `forge_schema` as public typed contract;
- backend parser types stay out of public modules;
- diagnostics use Forge schema diagnostics.

## Rejected Patterns

- Exposing backend XML node types or namespace-specific parser objects in public
  modules.
- Using libxml2 as the default backend for the first Forge XML slice; it is too
  broad for the required codec boundary.
- Building a custom XML parser/writer in Forge when a small backend can be
  wrapped cleanly.
- Putting S3 route matrices, bucket/object models, SigV4, storage policy or
  product error names into Forge.
- Allowing S3-style products to bypass `forge_http_api` for normal endpoints.
- Making XML the default codec for existing HTTP APIs.

## Test Mapping

- `test_forge_xml_backend_boundary`: no backend XML types in public module
  interfaces.
- `test_forge_xml_described_roundtrip`: Boost.Describe value roundtrip through
  XML with schema validation.
- `test_forge_xml_limits`: bounded size, depth, attribute and text handling.
- `test_forge_xml_rejects_unsafe_features`: DTD/entity/processing-instruction
  rejection.
- `test_forge_http_api_xml_roundtrip`: XML request and response bodies through
  typed HTTP API client/server.
- `test_forge_http_api_xml_errors`: XML error profile without JSON leakage.
- `test_forge_http_api_stream_modes`: file, bytes, stream and empty responses do
  not pass through XML/JSON DTO codecs.
