module;

export module forge.crypto.asymmetric.serialization;

export import forge.crypto.asymmetric.values;

import forge.core.utility;
import forge.variant.value;

export namespace forge::crypto::asymmetric {

void to_variant(const public_key& value, forge::variant& output,
                const forge::yield_function_t& yield = forge::yield_function_t());
void from_variant(const forge::variant& value, public_key& output);

void to_variant(const signature& value, forge::variant& output,
                const forge::yield_function_t& yield = forge::yield_function_t());
void from_variant(const forge::variant& value, signature& output);

} // namespace forge::crypto::asymmetric
