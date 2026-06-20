# HTTP FastAPI-Style Parameters

This design note records the implemented direction for multi-argument
`FCL_HTTP_API(...)` bindings and the next boundaries to keep clean.

The goal is FastAPI-style endpoint ergonomics for C++ without turning HTTP into
a generic frame RPC transport:

- `FCL_API(...)` remains the typed contract metadata source.
- `FCL_HTTP_API(...)` declares only HTTP presentation: method, path, success
  status and response mode.
- Request parameters are classified from C++ method argument types and described
  DTO fields.
- Boost.Beast request/parser/serializer mechanics stay private to `fcl_http`.
- `fcl_api` stays HTTP-free.

Donor behavior:

- FastAPI recognizes `Query`, `Path`, `Body`, `Cookie`, `Header`, `Form` and
  `File` as request parameter helpers.
- FastAPI also supports request/response context objects in path operation
  functions.
- FastAPI can inspect Python function parameter names at runtime. C++ cannot,
  so FCL records positional names in `FCL_API_METHOD(method, arg...)` while
  keeping HTTP parameter categories in wrapper types.

References:

- [FastAPI request parameters](https://fastapi.tiangolo.com/reference/parameters/)
- [FastAPI request body](https://fastapi.tiangolo.com/tutorial/body/)
- [FastAPI query parameters](https://fastapi.tiangolo.com/tutorial/query-params/)
- [FastAPI form data](https://fastapi.tiangolo.com/tutorial/request-forms/)
- [FastAPI response context](https://fastapi.tiangolo.com/advanced/response-headers/)

## Target Shape

Consumers should be able to write one C++ API method with several arguments:

```cpp
class object_api {
 public:
   virtual boost::asio::awaitable<object_meta>
   get_object(std::string bucket,
              std::string key,
              fcl::http::query<std::uint32_t> limit,
              fcl::http::header<std::string> request_id) = 0;
};

FCL_API(
   object_api,
   FCL_API_CONTRACT("object", 1, 0),
   FCL_API_METHOD(get_object, bucket, key, limit, request_id))
```

The HTTP presentation stays compact:

```cpp
FCL_HTTP_API(
   object_api,
   FCL_HTTP_GET(get_object, "/:bucket/:key?limit={limit}"))
```

There is no per-parameter macro mapping in the normal path. `FCL_HTTP_API(...)`
declares the route. The binding layer derives parameter sources:

- `bucket` and `key` are filled from `/:bucket/:key`.
- `query<std::uint32_t> limit` is filled from query parameter `limit`.
- `header<std::string> request_id` reads the default wire header
  `request-id`; explicit `FCL_HTTP_HEADER(request_id, "X-Request-ID")` can
  override that name.
- The generated client proxy builds the corresponding Boost.Beast request
  internally.

## Upload And Body Example

Streaming upload should not require a body mapping macro:

```cpp
struct put_object_path {
   bucket_name bucket;
   object_key key;
};

BOOST_DESCRIBE_STRUCT(put_object_path, (), (bucket, key))

struct content_type {
   std::string value;

   static constexpr std::string_view name = "Content-Type";
};

class object_api {
 public:
   virtual boost::asio::awaitable<put_object_response>
   put_object(std::string bucket,
              std::string key,
              fcl::http::header<std::string> type,
              fcl::http::body<put_object_payload> body) = 0;
};

FCL_API(
   object_api,
   FCL_API_CONTRACT("object", 1, 0),
   FCL_API_METHOD(put_object, bucket, key, type, body))

FCL_HTTP_API(
   object_api,
   FCL_HTTP_PUT(put_object, "/:bucket/:key", created))
```

Binding:

- `bucket` and `key` consume route path placeholders.
- `header<std::string>` consumes an HTTP header by argument name, or by
  explicit `FCL_HTTP_HEADER(...)` alias.
- `body<put_object_payload>` consumes an `application/json` body and decodes it
  with `fcl_json`.
- `body_stream` remains the streaming request-body type for APIs that should
  not buffer the entity body.

## Special Types

The first-class HTTP parameter vocabulary should be small and explicit:

| Type | Meaning |
| --- | --- |
| `fcl::http::query<T>` | Query parameter value decoded by argument name or route query alias. |
| `fcl::http::header<T>` | Header value decoded by explicit `FCL_HTTP_HEADER(...)` alias or default argument-name mapping `_ -> -`. |
| `fcl::http::cookie<T>` | Cookie value decoded by argument name. |
| `fcl::http::body<T>` | Explicit JSON body DTO when the method has several body-capable arguments. |
| `fcl::http::body_bytes` | Bounded raw body bytes. |
| `fcl::http::body_stream` | Streaming request body. |
| `fcl::http::form<T>` | Server-side form field value decoded by argument name or form alias. |
| `fcl::http::form_field<T>` | Server-side named form field. |
| `fcl::http::upload_file` | Server-side multipart file part with safe filename helpers and bounded spool behavior. |

The typed HTTP client supports ordinary JSON arguments, `query<T>`,
`header<T>`, `cookie<T>`, `body<T>`, `body_bytes` and `body_stream` without
falling back to `fcl.raw`. Browser-style multipart client construction for
`form<T>`, `form_field<T>` and `upload_file` is intentionally not routed through
generic API frames; it should be added as explicit HTTP client multipart
support rather than by serializing upload handles.

FastAPI-style background task injection is intentionally not part of this surface.
Background execution policy belongs to the application runtime, scheduler,
plugin lifecycle or product job system, not to HTTP parameter binding.

Response special types remain return values, not request parameters:

- `fcl::http::file_response`;
- `fcl::http::streaming_response`;
- `fcl::http::bytes_response`;
- `fcl::http::empty_response`.

## Binding Rules

Binding should be deterministic and fail closed:

1. Match path placeholders against positional argument names.
2. Decode `query<T>` by route query alias or argument name.
3. Decode `header<T>` by explicit HTTP header alias or default argument-name
   mapping `_ -> -`.
4. Decode `cookie<T>`, `form<T>` and `form_field<T>` by argument name unless an
   explicit alias is provided.
5. Bind `body_stream`, `body_bytes`, `upload_file` and explicit `body<T>` by
   type.
6. If there is exactly one ordinary described DTO on a body-capable route and
   it was not fully consumed by path/query/header binding, decode the JSON body
   into it and verify consistency for duplicate fields.
7. Run final `fcl_schema` validation after all HTTP sources are assembled.
8. Reject ambiguous mappings at compile time when the type information is
   enough, otherwise at mount time before the server starts.

There must be no silent guessing when two arguments could consume the same path,
query, header, cookie, form or body source.

## Bare Scalar Parameters

C++ does not expose function parameter names. Therefore this shape cannot be
made as strong as FastAPI without extra metadata:

```cpp
boost::asio::awaitable<fcl::http::file_response>
get_object(bucket_name bucket, object_key key);
```

FCL can support it only if one of these is true:

- `bucket_name` and `object_key` are named domain types with stable HTTP
  parameter metadata;
- the route has unambiguous positional path placeholders and the API explicitly
  accepts positional fallback;
- the method is rewritten to use a described request DTO such as
  `get_object_path`.

The preferred production shape is a described DTO for related path/body fields
and special FCL HTTP wrapper types for query, headers, cookies, forms, files and
request context.

## Error Model

HTTP parameter binding errors should project to stable HTTP responses:

- invalid target/path syntax -> `400 Bad Request`;
- missing or invalid path/query/header/cookie/form fields -> `422
  Unprocessable Entity`;
- unsupported body media type -> `415 Unsupported Media Type`;
- body or multipart limits -> `413 Payload Too Large`;
- ambiguous or invalid route binding -> mount-time typed error.

Diagnostics should use source-aware paths such as:

- `path.bucket`;
- `query.version_id`;
- `header.Authorization`;
- `cookie.session`;
- `body.metadata.content_type`;
- `form.avatar`;

Diagnostics must not include secrets, bearer tokens, cookies or raw private
values.

## Boundaries

- This is an HTTP presentation layer over `FCL_API(...)`, not a new product API
  framework.
- FCL does not add S3, SigV4, bucket, object-policy, billing or tenant-auth
  semantics.
- `fcl_api` must not import `fcl_http`.
- HTTP special parameter types make an API method HTTP-bound. Transport-neutral
  APIs should keep ordinary request/response DTOs.
- Boost.Beast remains a private mechanics dependency.
- Manual per-argument macros are only an escape hatch for rare ambiguous cases,
  not the normal API shape.
- Background task injection is out of scope for `fcl_http`; HTTP endpoints may
  submit work through explicit application/plugin APIs instead of hidden
  framework-managed post-response jobs.
