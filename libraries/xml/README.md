# forge_xml

`forge_xml` is the Forge typed XML codec. It provides a small Forge-owned XML
tree plus schema-backed `read` and `write` functions for APIs that exchange XML
bodies.

## When To Use

- Parse or write bounded XML request and response bodies.
- Map described C++ DTOs to XML elements using Boost.Describe and
  `forge_schema`.
- Use a small generic XML tree when a typed DTO is not the right shape.

## When Not To Use

- Do not use `forge_xml` as an HTTP router or transport binding. Use
  `forge_http_api` for typed HTTP routes.
- Do not use it for protocol-specific semantics, authentication, signing or
  storage policy. Those belong to the consuming product or protocol layer.
- Do not expose backend XML parser types in public APIs.

## Public Modules

- `forge.xml`

## Target And Component

- CMake target: `forge_xml`
- Package target: `Forge::forge_xml`
- Package component: `xml`

## Dependencies

- Public: `forge_core`, `forge_reflect`, `forge_schema`
- Private backend: vendored `pugixml`

`pugixml` is compiled directly into `forge_xml` with XPath and backend
exceptions disabled:

- `PUGIXML_NO_XPATH`
- `PUGIXML_NO_EXCEPTIONS`

Backend document and node types are not part of public modules, examples or
package consumer APIs.

## Examples

### Generic Tree

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

### Typed DTO

Typed DTOs use Boost.Describe plus optional `forge::schema::rules<T>` in the
same style as `forge_json` and `forge_yaml`. Schema field names become XML child
element names. Vectors are repeated child elements and empty `std::optional<T>`
values are omitted.

```cpp
#include <boost/describe.hpp>

import forge.schema.object;
import forge.xml;

struct resource_list {
   std::string name;
};

BOOST_DESCRIBE_STRUCT(resource_list, (), (name))

template <> struct forge::schema::rules<resource_list> {
   static forge::schema::object_schema<resource_list> define() {
      auto schema = forge::schema::object<resource_list>();
      schema.field<&resource_list::name>("Name").required();
      return schema;
   }
};

auto body = forge::xml::write(resource_list{.name = "primary"},
                              {.root_name = "ResourceList"});
```

## Boundaries And Safety

- `forge_xml` rejects DTD, entity declarations, processing instructions,
  comments and mixed content in the v1 public tree model.
- Use `read_options` and `write_options` to bound input size, output size,
  depth, attributes, children and text.
- XML diagnostics report schema paths and parse failures, but consumers should
  avoid placing secrets in element names, attribute names or diagnostic-visible
  values.
- Use `forge_schema` for validation. Do not add ad hoc parser-specific
  validation in consuming code.

## Common Mistakes

- Do not treat XML aliases as independent fields; schema aliases are alternate
  names for the same C++ member.
- Do not expect ordered mixed text and child segments; v1 fails closed instead
  of reordering content.
- Do not link or include `pugixml` from consumers. The backend is private to
  `forge_xml`.

## Tests

- `test_forge_xml`
- `test_forge_package_xml_component`
- `test_forge_package_xml_add_subdirectory`
