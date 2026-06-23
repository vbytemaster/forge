module;
#include <boost/dynamic_bitset.hpp>
#include <cstdint>

export module forge.variant.dynamic_bitset;

export namespace forge {

using dynamic_bitset = boost::dynamic_bitset<std::uint8_t>;

} // namespace forge
