# fcl_schema

`fcl_schema` describes typed configuration and codec rules for Boost.Describe
objects. It is intentionally small: metadata, defaults, basic validation and
diagnostics. It is not a full business validation framework.

## When To Use

- A typed config struct needs defaults, required fields, aliases, ranges,
  nested object-list rules or secret/deprecated metadata.
- JSON/YAML/CLI decoding must return path-aware diagnostics instead of throwing
  generic parser errors.
- A library wants to publish its config contract without depending on a parser.

## When Not To Use

- Do not put runtime state validation here.
- Do not perform security/authority checks here.
- Do not use schema metadata as a persistence migration system; migrations are
  a separate layer.

## Public Modules

- `fcl.schema.diagnostic` — `severity`, `diagnostic`.
- `fcl.schema.value_kind` — supported scalar/list kind metadata.
- `fcl.schema.object` — `rules<T>`, `object<T>()`, field builders.
- `fcl.schema.enums` — described enum conversion helpers.

Target: `fcl_schema`.

Dependencies: `fcl_reflect` and Boost.Describe headers.

## Examples

### Describe Config Rules

```cpp
#include <boost/describe.hpp>

#include <cstdint>
#include <string>

import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

struct http_config {
   std::uint16_t bind_port = 0;
   std::string bind_host;
   std::string token;
};

BOOST_DESCRIBE_STRUCT(http_config, (), (bind_port, bind_host, token))

template <>
struct fcl::schema::rules<http_config> {
   static fcl::schema::object_schema<http_config> define() {
      auto schema = fcl::schema::object<http_config>();
      schema.field<&http_config::bind_port>("bind-port")
         .alias("port")
         .required()
         .default_value(8080)
         .range(1, 65535)
         .description("Local HTTP listen port");
      schema.field<&http_config::bind_host>("bind-host").default_value("127.0.0.1");
      schema.field<&http_config::token>("token").secret().deprecated("use vault-ref");
      return schema;
   }
};
```

### Apply Defaults And Validate

```cpp
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

auto rules = fcl::schema::rules<http_config>::define();
auto config = http_config{};
rules.apply_defaults(config);
auto diagnostics = rules.validate(config, "http");
```

### Nested Object Lists

Use nested schemas for structured config fields such as local key entries. The
outer field stays one `object_list` for config registry, CLI and environment
adapters; nested fields still receive typed decode and indexed diagnostics.

```cpp
struct key_config {
   std::string id;
   std::string private_key;
   std::string input_profile = "fcl";
   std::vector<std::string> purposes;
};

struct signer_config {
   std::vector<key_config> keys;
};

BOOST_DESCRIBE_STRUCT(key_config, (), (id, private_key, input_profile, purposes))
BOOST_DESCRIBE_STRUCT(signer_config, (), (keys))

template <>
struct fcl::schema::rules<key_config> {
   static auto define() {
      auto schema = fcl::schema::object<key_config>();
      schema.field<&key_config::id>("id").required().non_empty();
      schema.field<&key_config::private_key>("private-key").required().secret();
      schema.field<&key_config::input_profile>("input-profile").default_value("fcl");
      schema.field<&key_config::purposes>("purposes").min_items(1).each_non_empty();
      return schema;
   }
};

template <>
struct fcl::schema::rules<signer_config> {
   static auto define() {
      auto schema = fcl::schema::object<signer_config>();
      schema.field<&signer_config::keys>("keys")
         .items<key_config>()
         .secret()
         .unique_by<&key_config::id>();
      return schema;
   }
};
```

### Convert Described Enums

```cpp
import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

auto level = fcl::schema::severity::info;
bool ok = fcl::schema::enum_from_string("warning", level);
auto name = fcl::schema::enum_to_string(fcl::schema::severity::error);
```

## Diagnostics Contract

Diagnostics carry:

- `path` — dotted config/value path;
- `code` — stable machine-readable reason;
- `level` — info/warning/error/critical;
- `message` — human-facing explanation.

## Risks And Anti-Patterns

- Do not use schema rules as authorization, live connectivity or credential
  validation. Schema validates local value shape before application checks run.
- Do not hide parser-specific decisions in schema metadata. JSON, YAML, env and
  CLI adapters each own their source diagnostics.
- Do not use nested object-list fields as generated CLI/environment fields.
  Keep the top-level object-list secret or source it from a protected config
  document.
- Do not mark a field `secret()` and then print the raw document. Redaction is an
  explicit config/log/UI step.

## Typical Mistakes

- Do not treat `secret()` as encryption. It only marks fields for redaction by
  config/log/UI layers.
- Do not assume `required()` overrides defaults; if a required field also has a
  default, consumers should decide whether defaults satisfy their contract.
- Do not put parser-specific behavior in schema. Parser adapters map their own
  errors into schema diagnostics.

## Tests

`test_fcl_schema` and `test_fcl_config` cover field traversal, defaults, range
validation, nested object-list decode, secret/deprecated metadata, and enum
string/int conversion.
