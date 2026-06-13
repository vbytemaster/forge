module;
#include <fcl/core/macros.hpp>
#include <boost/dynamic_bitset.hpp>
#include <stdexcept>
#include <string>
#include <utility>
export module fcl.variant.variant_dynamic_bitset;

import fcl.variant.exceptions;
import fcl.variant.value;
import fcl.variant.conversion;
import fcl.variant.containers;
import fcl.variant.chrono;
import fcl.variant.multiprecision;
import fcl.variant.format;
import fcl.variant.described;
import fcl.variant.dynamic_bitset;

export namespace fcl {
inline void to_variant(const fcl::dynamic_bitset& bs, fcl::variant& v) {
   auto num_blocks = bs.num_blocks();
   if (num_blocks > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("number of blocks of dynamic_bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS");

   std::string s;
   boost::to_string(bs, s);
   // From boost::dynamic_bitset docs:
   //   A character in the string is '1' if the corresponding bit is set, and '0' if it is not. Character
   //   position i in the string corresponds to bit position b.size() - 1 - i.
   v = std::move(s);
}

inline void from_variant(const fcl::variant& v, fcl::dynamic_bitset& bs) {
   std::string s = v.get_string();
   bs = fcl::dynamic_bitset(s);
}
} // namespace fcl
