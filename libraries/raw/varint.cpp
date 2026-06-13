module;
#include <cstdint>

module fcl.raw.varint;

import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;

namespace fcl {
void to_variant(const signed_int& var, variant& vo) {
   vo = var.value;
}
void from_variant(const variant& var, signed_int& vo) {
   vo.value = static_cast<int32_t>(var.as_int64());
}
void to_variant(const unsigned_int& var, variant& vo) {
   vo = var.value;
}
void from_variant(const variant& var, unsigned_int& vo) {
   vo.value = static_cast<uint32_t>(var.as_uint64());
}
} // namespace fcl
