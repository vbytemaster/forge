module;

module forge.crypto.asymmetric.serialization;

import forge.crypto.asymmetric;

namespace forge::crypto::asymmetric {

void to_variant(const public_key& value, forge::variant& output, const forge::yield_function_t& yield) {
   yield();
   output = encoding::forge().format(value);
}

void from_variant(const forge::variant& value, public_key& output) {
   output = encoding::forge().parse_public(value.as_string());
}

void to_variant(const signature& value, forge::variant& output, const forge::yield_function_t& yield) {
   yield();
   output = encoding::forge().format(value);
}

void from_variant(const forge::variant& value, signature& output) {
   output = encoding::forge().parse_signature(value.as_string());
}

} // namespace forge::crypto::asymmetric
