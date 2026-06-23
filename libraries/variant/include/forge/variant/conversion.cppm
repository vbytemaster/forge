module;
#include <memory>
#include <optional>
#include <utility>

export module forge.variant.conversion;

import forge.variant.value;

export namespace forge {
template <typename T> void to_variant(const std::optional<T>& value, variant& vo) {
   if (value.has_value()) {
      vo = variant(*value);
   } else {
      vo = nullptr;
   }
}

template <typename T> void from_variant(const variant& var, std::optional<T>& vo) {
   if (var.is_null()) {
      vo = std::optional<T>();
   } else {
      vo = T();
      from_variant(var, *vo);
   }
}

template <typename T> void to_variant(const std::shared_ptr<T>& var, variant& vo) {
   if (var) {
      to_variant(*var, vo);
   } else {
      vo = nullptr;
   }
}

template <typename T> void from_variant(const variant& var, std::shared_ptr<T>& vo) {
   if (var.is_null()) {
      vo = nullptr;
   } else if (vo) {
      from_variant(var, *vo);
   } else {
      vo = std::make_shared<T>();
      from_variant(var, *vo);
   }
}

template <typename T> void to_variant(const std::unique_ptr<T>& var, variant& vo) {
   if (var) {
      to_variant(*var, vo);
   } else {
      vo = nullptr;
   }
}

template <typename T> void from_variant(const variant& var, std::unique_ptr<T>& vo) {
   if (var.is_null()) {
      vo.reset();
   } else if (vo) {
      from_variant(var, *vo);
   } else {
      vo.reset(new T());
      from_variant(var, *vo);
   }
}

template <typename A, typename B> void to_variant(const std::pair<A, B>& t, variant& v) {
   variants vars(2);
   vars[0] = variant(t.first);
   vars[1] = variant(t.second);
   v = std::move(vars);
}

template <typename A, typename B> void from_variant(const variant& v, std::pair<A, B>& p) {
   const variants& vars = v.get_array();
   if (vars.size() > 0) {
      vars[0].as<A>(p.first);
   }
   if (vars.size() > 1) {
      vars[1].as<B>(p.second);
   }
}
} // namespace forge
