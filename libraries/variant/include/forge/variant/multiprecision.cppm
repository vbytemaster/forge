module;
#include <boost/multiprecision/cpp_int.hpp>

export module forge.variant.multiprecision;

import forge.variant.value;

export namespace forge {
template <typename T> void to_variant(const boost::multiprecision::number<T>& n, variant& v) {
   v = n.str();
}

template <typename T> void from_variant(const variant& v, boost::multiprecision::number<T>& n) {
   n = boost::multiprecision::number<T>(v.get_string());
}
} // namespace forge
