module;
#include <optional>
#include <type_traits>
#include <utility>

export module forge.variant.described;

import forge.reflect.reflect;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;

export namespace forge {

template <typename T>
void to_variant(const T& o, variant& v)
   requires forge::reflect::is_described_enum_v<T>
{
   v = forge::reflect::enum_to_fc_string(o);
}

template <typename T>
void from_variant(const variant& v, T& o)
   requires forge::reflect::is_described_enum_v<T>
{
   if (v.is_string()) {
      o = forge::reflect::enum_from_string<std::remove_const_t<T>>(v.get_string().c_str());
   } else {
      o = forge::reflect::enum_from_int<std::remove_const_t<T>>(v.as_int64());
   }
}

namespace detail {
template <typename M>
void add_described_member(mutable_variant_object& object, const char* name, const std::optional<M>& value) {
   if (value) {
      object(name, *value);
   }
}

template <typename M> void add_described_member(mutable_variant_object& object, const char* name, const M& value) {
   object(name, value);
}
} // namespace detail

template <typename T>
void to_variant(const T& o, variant& v)
   requires forge::reflect::is_described_object_v<T>
{
   mutable_variant_object object;
   forge::reflect::for_each_member<T>(
       [&](const char* name, auto member) { detail::add_described_member(object, name, o.*member); });
   v = std::move(object);
}

template <typename T>
void from_variant(const variant& v, T& o)
   requires forge::reflect::is_described_object_v<T>
{
   const variant_object& object = v.get_object();
   forge::reflect::for_each_member<std::remove_const_t<T>>([&](const char* name, auto member) {
      auto itr = object.find(name);
      if (itr != object.end()) {
         using member_type = std::remove_reference_t<decltype(o.*member)>;
         from_variant(itr->value(), const_cast<std::remove_const_t<member_type>&>(o.*member));
      }
   });
}

} // namespace forge
