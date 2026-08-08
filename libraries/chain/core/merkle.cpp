module;

#include <forge/exceptions/macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

module forge.chain.core.merkle;

import forge.raw.raw;

namespace forge::chain::core {
namespace {

digest hash_pair(const digest& left, const digest& right) {
   auto encoder = digest::encoder{};
   forge::raw::pack(encoder, left);
   forge::raw::pack(encoder, right);
   return encoder.result();
}

digest calculate_power_of_two_root(std::span<const digest> leaves) {
   if (leaves.size() == 1U) {
      return leaves.front();
   }

   const auto midpoint = leaves.size() / 2U;
   return hash_pair(calculate_power_of_two_root(leaves.first(midpoint)),
                    calculate_power_of_two_root(leaves.subspan(midpoint)));
}

std::size_t split_point(std::size_t count) {
   auto result = std::bit_floor(count);
   if (result == count) {
      result /= 2U;
   }
   return result;
}

void append_merkle_path(std::span<const digest> leaves, std::size_t index, std::vector<merkle_step>& result) {
   if (leaves.size() == 1U) {
      return;
   }

   const auto midpoint = split_point(leaves.size());
   if (index < midpoint) {
      append_merkle_path(leaves.first(midpoint), index, result);
      result.push_back({.sibling = calculate_merkle_root(leaves.subspan(midpoint)), .sibling_on_left = false});
   } else {
      append_merkle_path(leaves.subspan(midpoint), index - midpoint, result);
      result.push_back({.sibling = calculate_merkle_root(leaves.first(midpoint)), .sibling_on_left = true});
   }
}

std::vector<bool> merkle_path_sides(std::uint64_t index, std::uint64_t count) {
   auto root_to_leaf = std::vector<bool>{};
   while (count > 1U) {
      const auto midpoint = split_point(static_cast<std::size_t>(count));
      if (index < midpoint) {
         root_to_leaf.push_back(false);
         count = midpoint;
      } else {
         root_to_leaf.push_back(true);
         index -= midpoint;
         count -= midpoint;
      }
   }
   return {root_to_leaf.rbegin(), root_to_leaf.rend()};
}

} // namespace

digest calculate_merkle_root(std::span<const digest> leaves) {
   if (leaves.empty()) {
      return {};
   }
   if (leaves.size() == 1U) {
      return leaves.front();
   }

   const auto midpoint = std::bit_floor(leaves.size());
   if (midpoint == leaves.size()) {
      return calculate_power_of_two_root(leaves);
   }

   return hash_pair(calculate_power_of_two_root(leaves.first(midpoint)),
                    calculate_merkle_root(leaves.subspan(midpoint)));
}

std::vector<merkle_step> calculate_merkle_path(std::span<const digest> leaves, std::uint64_t index) {
   if (leaves.empty() || index >= leaves.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_leaf_index, "merkle leaf index is outside the tree",
                            forge::exceptions::ctx("index", index),
                            forge::exceptions::ctx("leaf_count", leaves.size()));
   }

   auto result = std::vector<merkle_step>{};
   result.reserve(std::bit_width(leaves.size()));
   append_merkle_path(leaves, static_cast<std::size_t>(index), result);
   return result;
}

bool verify_merkle_path(const digest& leaf, std::uint64_t index, std::uint64_t leaf_count,
                        std::span<const merkle_step> path, const digest& expected_root) {
   if (leaf_count == 0U || index >= leaf_count) {
      return false;
   }

   const auto sides = merkle_path_sides(index, leaf_count);
   if (path.size() != sides.size()) {
      return false;
   }

   auto current = leaf;
   for (auto position = std::size_t{0}; position < path.size(); ++position) {
      if (path[position].sibling_on_left != sides[position]) {
         return false;
      }
      current = path[position].sibling_on_left ? hash_pair(path[position].sibling, current)
                                               : hash_pair(current, path[position].sibling);
   }
   return current == expected_root;
}

void incremental_merkle_tree::append(const digest& leaf) {
   if (mask_ == std::numeric_limits<std::uint64_t>::max()) {
      FORGE_THROW_EXCEPTION(exceptions::leaf_count_overflow, "incremental merkle tree leaf count overflow");
   }

   auto trees = trees_;
   auto root = leaf;
   auto rank = std::size_t{0};
   while ((mask_ & (std::uint64_t{1} << rank)) != 0U) {
      root = hash_pair(trees.back(), root);
      trees.pop_back();
      ++rank;
   }

   trees.push_back(root);
   trees_ = std::move(trees);
   ++mask_;
}

digest incremental_merkle_tree::root() const {
   if (trees_.empty()) {
      return {};
   }

   auto result = trees_.back();
   for (auto index = trees_.size() - 1U; index > 0U; --index) {
      result = hash_pair(trees_[index - 1U], result);
   }
   return result;
}

std::uint64_t incremental_merkle_tree::size() const noexcept {
   return mask_;
}

bool incremental_merkle_tree::empty() const noexcept {
   return mask_ == 0U;
}

} // namespace forge::chain::core
