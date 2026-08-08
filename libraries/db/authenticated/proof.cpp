module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

module forge.db.authenticated.proof;

import forge.db.authenticated.exceptions;
import forge.db.authenticated.codec;
import forge.db.authenticated.hash;

namespace forge::db::authenticated {

namespace {

bool key_less(std::span<const std::byte> left, std::span<const std::byte> right) {
   return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
}

[[noreturn]] void reject(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_proof, message);
}

struct node_metadata {
   digest hash;
   std::uint16_t height = 0;
   std::uint64_t size = 0;
   bytes min_key;
   bytes max_key;
};

void require_key_bound(const bytes& value, const limits& settings, std::string_view message) {
   if (value.size() > settings.max_key_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, message);
   }
}

node_metadata combine(std::string_view domain, std::uint16_t height, std::uint64_t size, const bytes& min_key,
                      const bytes& max_key, const bytes& separator, const node_metadata& left,
                      const node_metadata& right) {
   if (left.size > std::numeric_limits<std::uint64_t>::max() - right.size || size != left.size + right.size) {
      reject("authenticated proof subtree size is inconsistent");
   }
   const auto expected_height = static_cast<std::uint32_t>(std::max(left.height, right.height)) + 1U;
   if (expected_height > std::numeric_limits<std::uint16_t>::max() || height != expected_height ||
       std::abs(static_cast<int>(left.height) - static_cast<int>(right.height)) > 1) {
      reject("authenticated proof AVL metadata is inconsistent");
   }
   if (!key_less(left.max_key, right.min_key) || separator != right.min_key || min_key != left.min_key ||
       max_key != right.max_key) {
      reject("authenticated proof ordered bounds are inconsistent");
   }
   return {
       .hash = hash_inner(domain, height, size, min_key, max_key, separator, left.hash, right.hash),
       .height = height,
       .size = size,
       .min_key = min_key,
       .max_key = max_key,
   };
}

node_metadata verify_sibling(std::string_view domain, const proof_sibling& sibling, const limits& settings) {
   if (sibling.valueless_by_exception()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_proof, "authenticated point proof sibling has no value");
   }
   return std::visit(
       [&](const auto& value) -> node_metadata {
          using value_type = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::same_as<value_type, proof_leaf>) {
             if (value.key.size() > settings.max_key_bytes) {
                FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded,
                                      "authenticated sibling leaf key exceeds configured limit");
             }
             if (value.value &&
                 (value.value->size() > settings.max_value_bytes || hash_value(*value.value) != value.value_hash)) {
                reject("authenticated sibling leaf value is invalid");
             }
             return {
                 .hash = hash_leaf(domain, value.key, value.value_hash),
                 .height = 0,
                 .size = 1,
                 .min_key = value.key,
                 .max_key = value.key,
             };
          } else {
             require_key_bound(value.min_key, settings,
                               "authenticated sibling branch minimum key exceeds configured limit");
             require_key_bound(value.max_key, settings,
                               "authenticated sibling branch maximum key exceeds configured limit");
             require_key_bound(value.separator, settings,
                               "authenticated sibling branch separator exceeds configured limit");
             if (value.height == 0 || value.size < 2 || !key_less(value.min_key, value.max_key) ||
                 !key_less(value.min_key, value.separator) || key_less(value.max_key, value.separator)) {
                reject("authenticated sibling branch metadata is invalid");
             }
             return {
                 .hash = hash_inner(domain, value.height, value.size, value.min_key, value.max_key, value.separator,
                                    value.left_hash, value.right_hash),
                 .height = value.height,
                 .size = value.size,
                 .min_key = value.min_key,
                 .max_key = value.max_key,
             };
          }
       },
       sibling);
}

struct ranked_leaf {
   proof_leaf leaf;
   std::uint64_t rank = 0;
};

class range_parser {
 public:
   range_parser(std::string_view domain, const range_proof& proof, const limits& settings)
       : domain_{domain}, proof_{proof}, settings_{settings} {
      if (proof.nodes.size() > settings.max_proof_nodes) {
         FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated range proof exceeds node limit");
      }
   }

   node_metadata parse() {
      const auto result = parse_node(0, 0);
      if (next_ != proof_.nodes.size()) {
         reject("authenticated range proof has trailing nodes");
      }
      return result;
   }

   [[nodiscard]] const std::vector<ranked_leaf>& leaves() const noexcept {
      return leaves_;
   }

 private:
   node_metadata parse_node(std::uint64_t offset, std::uint32_t depth) {
      if (depth > settings_.max_proof_depth) {
         FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated range proof exceeds depth limit");
      }
      if (next_ >= proof_.nodes.size()) {
         reject("authenticated range proof is truncated");
      }

      const auto& encoded = proof_.nodes[next_++];
      if (const auto* branch = std::get_if<proof_branch>(&encoded)) {
         require_key_bound(branch->min_key, settings_,
                           "authenticated opaque branch minimum key exceeds configured limit");
         require_key_bound(branch->max_key, settings_,
                           "authenticated opaque branch maximum key exceeds configured limit");
         require_key_bound(branch->separator, settings_,
                           "authenticated opaque branch separator exceeds configured limit");
         if (branch->height == 0 || branch->size < 2 || !key_less(branch->min_key, branch->max_key) ||
             !key_less(branch->min_key, branch->separator) || key_less(branch->max_key, branch->separator)) {
            reject("authenticated opaque branch metadata is invalid");
         }
         return {
             .hash = hash_inner(domain_, branch->height, branch->size, branch->min_key, branch->max_key,
                                branch->separator, branch->left_hash, branch->right_hash),
             .height = branch->height,
             .size = branch->size,
             .min_key = branch->min_key,
             .max_key = branch->max_key,
         };
      }
      if (const auto* leaf = std::get_if<proof_leaf>(&encoded)) {
         if (leaf->key.size() > settings_.max_key_bytes ||
             (leaf->value &&
              (leaf->value->size() > settings_.max_value_bytes || hash_value(*leaf->value) != leaf->value_hash))) {
            reject("authenticated range leaf is invalid");
         }
         leaves_.push_back(ranked_leaf{.leaf = *leaf, .rank = offset});
         return {
             .hash = hash_leaf(domain_, leaf->key, leaf->value_hash),
             .height = 0,
             .size = 1,
             .min_key = leaf->key,
             .max_key = leaf->key,
         };
      }

      const auto* inner = std::get_if<range_inner>(&encoded);
      if (!inner) {
         reject("authenticated range proof contains an invalid node alternative");
      }
      require_key_bound(inner->min_key, settings_,
                        "authenticated expanded branch minimum key exceeds configured limit");
      require_key_bound(inner->max_key, settings_,
                        "authenticated expanded branch maximum key exceeds configured limit");
      require_key_bound(inner->separator, settings_,
                        "authenticated expanded branch separator exceeds configured limit");
      if (inner->height == 0 || inner->size < 2) {
         reject("authenticated expanded branch metadata is invalid");
      }
      const auto left = parse_node(offset, depth + 1U);
      if (offset > std::numeric_limits<std::uint64_t>::max() - left.size) {
         reject("authenticated range rank overflows");
      }
      const auto right = parse_node(offset + left.size, depth + 1U);
      return combine(domain_, inner->height, inner->size, inner->min_key, inner->max_key, inner->separator, left,
                     right);
   }

   std::string_view domain_;
   const range_proof& proof_;
   const limits& settings_;
   std::size_t next_ = 0;
   std::vector<ranked_leaf> leaves_;
};

const ranked_leaf* find_rank(const std::vector<ranked_leaf>& leaves, std::uint64_t rank) {
   const auto found =
       std::lower_bound(leaves.begin(), leaves.end(), rank,
                        [](const ranked_leaf& value, std::uint64_t candidate) { return value.rank < candidate; });
   return found != leaves.end() && found->rank == rank ? std::addressof(*found) : nullptr;
}

} // namespace

verified_point verify_point(std::string_view domain, const root& expected_anchor,
                            std::span<const std::byte> expected_key, const point_proof& proof, const limits& settings) {
   if (domain.empty() || domain.size() > max_base_domain_bytes) {
      reject("authenticated proof domain is empty");
   }
   require_wire_budget(0, settings);
   if (proof.anchor != expected_anchor || !std::ranges::equal(proof.key, expected_key)) {
      reject("authenticated point proof does not match the requested anchor or key");
   }
   if (proof.key.size() > settings.max_key_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated proof key exceeds configured limit");
   }
   if (proof.path.size() > settings.max_proof_depth ||
       proof.path.size() > (settings.max_proof_nodes == 0 ? 0 : (settings.max_proof_nodes - 1U) / 2U)) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated proof depth exceeds configured limit");
   }
   require_wire_budget(wire_size(proof), settings);
   const auto tree_domain = canonical_tree_domain(domain, proof_tree::state);

   for (const auto& step : proof.path) {
      require_key_bound(step.min_key, settings, "authenticated proof minimum key exceeds configured limit");
      require_key_bound(step.max_key, settings, "authenticated proof maximum key exceeds configured limit");
      require_key_bound(step.separator, settings, "authenticated proof separator exceeds configured limit");
   }

   if (!proof.terminal) {
      if (!proof.path.empty() || proof.anchor.state_size != 0 || proof.anchor.state_root != hash_empty(tree_domain)) {
         reject("authenticated empty-tree proof is inconsistent");
      }
      return {};
   }

   if (proof.terminal->key.size() > settings.max_key_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated terminal key exceeds configured limit");
   }
   if (proof.terminal->value && proof.terminal->value->size() > settings.max_value_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated proof value exceeds configured limit");
   }
   if (proof.terminal->value && hash_value(*proof.terminal->value) != proof.terminal->value_hash) {
      reject("authenticated proof value hash is invalid");
   }

   auto current = node_metadata{
       .hash = hash_leaf(tree_domain, proof.terminal->key, proof.terminal->value_hash),
       .height = 0,
       .size = 1,
       .min_key = proof.terminal->key,
       .max_key = proof.terminal->key,
   };
   auto rank = std::uint64_t{0};

   if (key_less(proof.terminal->key, proof.key)) {
      rank = 1;
   }

   for (const auto& step : proof.path) {
      if (step.child != branch_side::left && step.child != branch_side::right) {
         reject("authenticated proof branch side is invalid");
      }
      const auto go_left = key_less(proof.key, step.separator);
      if ((step.child == branch_side::left) != go_left) {
         reject("authenticated proof follows the wrong search branch");
      }
      const auto sibling = verify_sibling(tree_domain, step.sibling, settings);
      if (step.child == branch_side::left) {
         current = combine(tree_domain, step.height, step.subtree_size, step.min_key, step.max_key, step.separator,
                           current, sibling);
      } else {
         if (rank > std::numeric_limits<std::uint64_t>::max() - sibling.size) {
            reject("authenticated proof rank overflows");
         }
         rank += sibling.size;
         current = combine(tree_domain, step.height, step.subtree_size, step.min_key, step.max_key, step.separator,
                           sibling, current);
      }
   }

   if (current.hash != proof.anchor.state_root || current.size != proof.anchor.state_size) {
      reject("authenticated proof does not reconstruct the anchored root");
   }

   const auto exists = proof.terminal->key == proof.key;
   return verified_point{
       .exists = exists,
       .value_hash = exists ? std::optional<digest>{proof.terminal->value_hash} : std::nullopt,
       .value = exists ? proof.terminal->value : std::nullopt,
       .rank = rank,
   };
}

verified_range verify_range(std::string_view domain, const root& expected_anchor, const range_request& expected_request,
                            proof_tree expected_tree, const range_proof& proof, const limits& settings) {
   if (domain.empty() || domain.size() > max_base_domain_bytes) {
      reject("authenticated range proof domain is empty");
   }
   require_wire_budget(0, settings);
   if ((expected_tree != proof_tree::state && expected_tree != proof_tree::changes) ||
       (proof.tree != proof_tree::state && proof.tree != proof_tree::changes)) {
      reject("authenticated proof tree is invalid");
   }
   if (proof.anchor != expected_anchor || proof.request != expected_request || proof.tree != expected_tree) {
      reject("authenticated range proof does not match the requested anchor or range");
   }
   if (proof.request.limit == 0 || proof.request.limit > settings.max_range_items) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated range limit exceeds configured limit");
   }
   if ((proof.request.lower && proof.request.lower->size() > settings.max_key_bytes) ||
       (proof.request.upper && proof.request.upper->size() > settings.max_key_bytes)) {
      FORGE_THROW_EXCEPTION(exceptions::proof_limit_exceeded, "authenticated range boundary exceeds key limit");
   }
   if (proof.request.lower && proof.request.upper && !key_less(*proof.request.lower, *proof.request.upper)) {
      reject("authenticated proof range must be non-empty");
   }

   const auto tree_domain = canonical_tree_domain(domain, proof.tree);
   const auto expected_root = proof.tree == proof_tree::state ? proof.anchor.state_root : proof.anchor.change_root;
   const auto total = proof.tree == proof_tree::state ? proof.anchor.state_size : proof.anchor.change_count;
   require_wire_budget(wire_size(proof), settings);
   if (total == 0) {
      if (!proof.nodes.empty() || expected_root != hash_empty(tree_domain)) {
         reject("authenticated empty range proof is inconsistent");
      }
      return {};
   }
   if (proof.nodes.empty()) {
      reject("authenticated range proof omits its tree");
   }

   auto parser = range_parser{tree_domain, proof, settings};
   const auto parsed = parser.parse();
   if (parsed.hash != expected_root || parsed.size != total) {
      reject("authenticated range proof does not reconstruct the anchored root");
   }
   const auto& leaves = parser.leaves();
   if (leaves.empty()) {
      reject("authenticated range proof omits its boundary leaves");
   }
   for (auto index = std::size_t{1}; index < leaves.size(); ++index) {
      if (leaves[index - 1U].rank >= leaves[index].rank ||
          !key_less(leaves[index - 1U].leaf.key, leaves[index].leaf.key)) {
         reject("authenticated range leaves are not strictly ordered");
      }
   }

   auto lower_rank = std::uint64_t{};
   if (proof.request.lower) {
      const auto candidate = std::find_if(leaves.begin(), leaves.end(), [&](const ranked_leaf& value) {
         return !key_less(value.leaf.key, *proof.request.lower);
      });
      if (candidate == leaves.end()) {
         const auto& last = leaves.back();
         if (last.rank != total - 1U || !key_less(last.leaf.key, *proof.request.lower)) {
            reject("authenticated range proof omits its lower boundary");
         }
         lower_rank = total;
      } else {
         lower_rank = candidate->rank;
         if (lower_rank != 0) {
            const auto* predecessor = find_rank(leaves, lower_rank - 1U);
            if (!predecessor || !key_less(predecessor->leaf.key, *proof.request.lower)) {
               reject("authenticated range predecessor is invalid");
            }
         }
      }
   } else if (!find_rank(leaves, 0)) {
      reject("authenticated unbounded range omits the first leaf");
   }

   auto upper_rank = total;
   if (proof.request.reverse) {
      if (proof.request.upper) {
         const auto candidate = std::find_if(leaves.begin(), leaves.end(), [&](const ranked_leaf& value) {
            return !key_less(value.leaf.key, *proof.request.upper);
         });
         if (candidate == leaves.end()) {
            const auto& last = leaves.back();
            if (last.rank != total - 1U || !key_less(last.leaf.key, *proof.request.upper)) {
               reject("authenticated range proof omits its upper boundary");
            }
         } else {
            upper_rank = candidate->rank;
            if (upper_rank != 0U) {
               const auto* predecessor = find_rank(leaves, upper_rank - 1U);
               if (!predecessor || !key_less(predecessor->leaf.key, *proof.request.upper)) {
                  reject("authenticated range upper predecessor is invalid");
               }
            }
         }
      } else if (!find_rank(leaves, total - 1U)) {
         reject("authenticated unbounded range omits the last leaf");
      }
      if (lower_rank > upper_rank) {
         reject("authenticated range bounds select inconsistent ranks");
      }
   }

   auto result = verified_range{.total_size = total};
   if (proof.request.reverse) {
      auto rank = upper_rank;
      while (rank > lower_rank && result.items.size() < proof.request.limit) {
         --rank;
         const auto* current = find_rank(leaves, rank);
         if (!current) {
            reject("authenticated reverse range proof has a gap");
         }
         if (proof.request.include_values && !current->leaf.value) {
            reject("authenticated range proof omits a requested value");
         }
         result.items.push_back(verified_range_item{
             .key = current->leaf.key,
             .value_hash = current->leaf.value_hash,
             .value = current->leaf.value,
             .rank = rank,
         });
      }
      if (rank > lower_rank) {
         const auto* predecessor = find_rank(leaves, rank - 1U);
         if (!predecessor) {
            reject("authenticated reverse range proof omits its continuation");
         }
         if (result.items.size() != proof.request.limit) {
            reject("authenticated reverse range proof omits a matching item");
         }
         result.more = true;
         result.next_key = result.items.back().key;
      }
   } else {
      auto rank = lower_rank;
      while (rank < total && result.items.size() < proof.request.limit) {
         const auto* current = find_rank(leaves, rank);
         if (!current) {
            reject("authenticated range proof has a gap");
         }
         if (proof.request.upper && !key_less(current->leaf.key, *proof.request.upper)) {
            break;
         }
         if (proof.request.include_values && !current->leaf.value) {
            reject("authenticated range proof omits a requested value");
         }
         result.items.push_back(verified_range_item{
             .key = current->leaf.key,
             .value_hash = current->leaf.value_hash,
             .value = current->leaf.value,
             .rank = rank,
         });
         ++rank;
      }

      if (rank < total) {
         const auto* successor = find_rank(leaves, rank);
         if (!successor) {
            reject("authenticated range proof omits its upper boundary");
         }
         result.more = !proof.request.upper || key_less(successor->leaf.key, *proof.request.upper);
         if (result.more) {
            if (result.items.size() != proof.request.limit) {
               reject("authenticated range proof omits a matching item");
            }
            result.next_key = successor->leaf.key;
         }
      }
   }
   return result;
}

} // namespace forge::db::authenticated
