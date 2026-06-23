module;
#include <forge/core/macros.hpp>
#include <boost/dynamic_bitset.hpp>
#include <stdexcept>
#include <string>
#include <utility>
export module forge.variant.variant_dynamic_bitset;

import forge.variant.exceptions;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;
import forge.variant.dynamic_bitset;

export namespace forge {
inline void to_variant(const forge::dynamic_bitset& bs, forge::variant& v) {
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

inline void from_variant(const forge::variant& v, forge::dynamic_bitset& bs) {
   std::string s = v.get_string();
   bs = forge::dynamic_bitset(s);
}
} // namespace forge
