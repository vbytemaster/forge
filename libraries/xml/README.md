# forge_xml

`forge_xml` is the Forge typed XML codec. It provides a small Forge-owned XML tree and
schema-backed typed `read`/`write` functions for APIs that use XML bodies, including
S3-style protocols.

## Public Modules

- `forge.xml`

## Target

- CMake target: `forge_xml`
- Package target: `Forge::forge_xml`
- Package component: `xml`

## Backend

`forge_xml` uses vendored `pugixml` privately. Backend document/node types are not part
of the public API, public modules, examples, or package consumer surface.

The target compiles `pugixml.cpp` directly with XPath and backend exceptions disabled:

- `PUGIXML_NO_XPATH`
- `PUGIXML_NO_EXCEPTIONS`

## Usage

```cpp
import forge.xml;

auto parsed = forge::xml::read_value("<Root><Item id=\"1\">text</Item></Root>");
if (!parsed.ok()) {
   // Inspect parsed.diagnostics.
}

auto written = forge::xml::write_value(
   forge::xml::document{
      .root = forge::xml::element{.name = "Root", .text = "text"},
   });
```

Typed DTOs use Boost.Describe plus optional `forge::schema::rules<T>` in the same style
as `forge_json` and `forge_yaml`. Schema field names become XML child element names.
Vectors are repeated child elements and empty `std::optional<T>` values are omitted.

```cpp
struct list_bucket_result {
   std::string name;
};

BOOST_DESCRIBE_STRUCT(list_bucket_result, (), (name))

template <> struct forge::schema::rules<list_bucket_result> {
   static forge::schema::object_schema<list_bucket_result> define() {
      auto schema = forge::schema::object<list_bucket_result>();
      schema.field<&list_bucket_result::name>("Name").required();
      return schema;
   }
};

auto body = forge::xml::write(list_bucket_result{.name = "photos"},
                              {.root_name = "ListBucketResult"});
```

## Boundaries

- Do not use `forge_xml` as an HTTP router or S3 protocol layer. HTTP binding belongs to
  `forge_http_api`; S3 semantics belong to downstream products.
- Do not expose or store backend parser types in consumer APIs.
- Do not enable DTD, entity processing, processing instructions, or comments for API
  request bodies. `forge_xml` fails closed on these features.
- Use limits in `read_options` and `write_options` for request-body and response-body
  bounds.

## Tests

- `test_forge_xml`
- `test_forge_package_xml_component`
