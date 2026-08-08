module;

#include <string>

export module forge.chain.api.json_schema;

import forge.api.http.openapi;
import forge.chain.protocol.types;
import forge.variant.value;

export namespace forge::api::http {

template <> struct json_schema_traits<forge::chain::protocol::public_key> {
   [[nodiscard]] static forge::variant make() {
      return forge::variant{forge::mutable_variant_object{}("type", "string")("format", "forge-public-key")};
   }
};

template <> struct json_schema_traits<forge::chain::protocol::signature> {
   [[nodiscard]] static forge::variant make() {
      return forge::variant{forge::mutable_variant_object{}("type", "string")("format", "forge-signature")};
   }
};

} // namespace forge::api::http
