module;
#include <forge/core/macros.hpp>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/multi_index_container_fwd.hpp>

export module forge.variant.containers;

import forge.variant.value;
import forge.variant.conversion;

export namespace forge {
template <typename T> void to_variant(const std::unordered_set<T>& var, variant& vo) {
   if (var.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   variants vars(var.size());
   size_t i = 0;
   for (auto itr = var.begin(); itr != var.end(); ++itr, ++i) {
      vars[i] = variant(*itr);
   }
   vo = std::move(vars);
}

template <typename T> void from_variant(const variant& var, std::unordered_set<T>& vo) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   vo.clear();
   vo.reserve(vars.size());
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      vo.insert(itr->as<T>());
   }
}

template <typename K, typename T> void to_variant(const std::unordered_map<K, T>& var, variant& vo) {
   if (var.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   std::vector<variant> vars(var.size());
   size_t i = 0;
   for (auto itr = var.begin(); itr != var.end(); ++itr, ++i) {
      vars[i] = variant(*itr);
   }
   vo = std::move(vars);
}

template <typename K, typename T> void from_variant(const variant& var, std::unordered_map<K, T>& vo) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   vo.clear();
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      vo.insert(itr->as<std::pair<K, T>>());
   }
}

template <typename K, typename T> void to_variant(const std::map<K, T>& var, variant& vo) {
   if (var.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   std::vector<variant> vars(var.size());
   size_t i = 0;
   for (auto itr = var.begin(); itr != var.end(); ++itr, ++i) {
      vars[i] = variant(*itr);
   }
   vo = std::move(vars);
}

template <typename K, typename T> void from_variant(const variant& var, std::map<K, T>& vo) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   vo.clear();
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      vo.insert(itr->as<std::pair<K, T>>());
   }
}

template <typename K, typename T> void to_variant(const std::multimap<K, T>& var, variant& vo) {
   if (var.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   std::vector<variant> vars(var.size());
   size_t i = 0;
   for (auto itr = var.begin(); itr != var.end(); ++itr, ++i) {
      vars[i] = variant(*itr);
   }
   vo = std::move(vars);
}

template <typename K, typename T> void from_variant(const variant& var, std::multimap<K, T>& vo) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   vo.clear();
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      vo.insert(itr->as<std::pair<K, T>>());
   }
}

template <typename T> void to_variant(const std::set<T>& var, variant& vo) {
   if (var.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   variants vars(var.size());
   size_t i = 0;
   for (auto itr = var.begin(); itr != var.end(); ++itr, ++i) {
      vars[i] = variant(*itr);
   }
   vo = std::move(vars);
}

template <typename T> void from_variant(const variant& var, std::set<T>& vo) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   vo.clear();
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      vo.insert(itr->as<T>());
   }
}

template <typename T> void from_variant(const variant& var, std::deque<T>& tmp) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   tmp.clear();
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      tmp.push_back(itr->as<T>());
   }
}

template <typename T> void to_variant(const std::deque<T>& t, variant& v) {
   if (t.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   variants vars(t.size());
   for (size_t i = 0; i < t.size(); ++i) {
      vars[i] = variant(t[i]);
   }
   v = std::move(vars);
}

template <typename T> void from_variant(const variant& var, std::vector<T>& tmp) {
   const variants& vars = var.get_array();
   if (vars.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   tmp.clear();
   tmp.reserve(vars.size());
   for (auto itr = vars.begin(); itr != vars.end(); ++itr) {
      tmp.push_back(itr->as<T>());
   }
}

template <typename T> void to_variant(const std::vector<T>& t, variant& v) {
   if (t.size() > MAX_NUM_ARRAY_ELEMENTS)
      throw std::range_error("too large");
   variants vars(t.size());
   for (size_t i = 0; i < t.size(); ++i) {
      vars[i] = variant(t[i]);
   }
   v = std::move(vars);
}

template <typename T, std::size_t S> void from_variant(const variant& var, std::array<T, S>& tmp) {
   const variants& vars = var.get_array();
   if (vars.size() != S)
      throw std::length_error("mismatch between variant vector size and expected array size");
   for (std::size_t i = 0; i < S; ++i) {
      tmp[i] = vars.at(i).as<T>();
   }
}

template <typename T, std::size_t S> void to_variant(const std::array<T, S>& t, variant& v) {
   variants vars(S);
   for (std::size_t i = 0; i < S; ++i) {
      vars[i] = variant(t[i]);
   }
   v = std::move(vars);
}

template <typename T> void to_variant(const std::initializer_list<T>& t, variant& v) {
   auto sz = t.size();
   variants vars(sz);
   for (std::size_t i = 0; i < sz; ++i) {
      vars[i] = variant(*(t.begin() + i));
   }
   v = std::move(vars);
}

template <typename T, typename... Args> void to_variant(const boost::multi_index_container<T, Args...>& c, variant& v) {
   variants vars;
   vars.reserve(c.size());
   for (const auto& item : c) {
      vars.emplace_back(variant(item));
   }
   v = std::move(vars);
}

template <typename T, typename... Args>
void from_variant(const variant& v, boost::multi_index_container<T, Args...>& c) {
   const variants& vars = v.get_array();
   c.clear();
   for (const auto& item : vars) {
      c.insert(item.as<T>());
   }
}
} // namespace forge
