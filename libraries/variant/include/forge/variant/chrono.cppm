module;
#include <chrono>

export module forge.variant.chrono;

import forge.variant.value;

export namespace forge {
void to_variant(const std::chrono::sys_time<std::chrono::microseconds>& var, variant& vo);
void from_variant(const variant& var, std::chrono::sys_time<std::chrono::microseconds>& vo);
void to_variant(const std::chrono::sys_seconds& var, variant& vo);
void from_variant(const variant& var, std::chrono::sys_seconds& vo);
void to_variant(const std::chrono::microseconds& input_microseconds, variant& output_variant);
void from_variant(const variant& input_variant, std::chrono::microseconds& output_microseconds);
} // namespace forge
