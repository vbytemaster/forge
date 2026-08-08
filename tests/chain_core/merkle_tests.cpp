#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.chain.core.merkle;
import forge.crypto.digest.sha256;
import forge.raw.exceptions;
import forge.raw.raw;

namespace core = forge::chain::core;

namespace {

constexpr auto modern_roots = std::array<std::string_view, 10>{
    "0000000000000000000000000000000000000000000000000000000000000000",
    "fea82e10e894419fe2bea7d96296a6d46f50f93f9eeda954ec461b2ed2950b62",
    "079f3d94282078fb08755b501f8a46ad66ba285986c6d31d688d1ae32598dc92",
    "2bc14b1b208002a84869667027524fc1e9b412efc8ca7cc11c8bff8c964c7bc7",
    "1fcc820563a6deb90991447529903dd43d2c19514dd0e28316508c16fa0ce2b9",
    "50c9998d194e194e49860f8642ca882ba65a06bc9ca2f85a9c9093cd44a4e71b",
    "d894b114bc9d8942796cce96d84b112e0653f841ad808fcb67b0d0007aa0c2c0",
    "dfb683c63b68427390d21e351fac541f291879b6957a49b673d38e95134c768b",
    "855f4d1bdc54116ad144d5b6d03991e84115060f70d49f4bbc895d7bae2184e9",
    "29a9c6d871ee6cf1e0ffc5b8c7098b2fd1bcd12cdc4795d6e37323db77aedd83",
};

constexpr auto spring_state_after_five =
    std::string_view{"050000000000000002"
                     "1fcc820563a6deb90991447529903dd43d2c19514dd0e28316508c16fa0ce2b9"
                     "5abef8bc27d85d53753c5b6ed0cd2e197998c21513a379bfcf44d9c7a73c3a7e"};

forge::raw::bytes unhex(std::string_view value) {
   auto out = forge::raw::bytes{};
   out.reserve(value.size() / 2U);
   for (auto index = std::size_t{0}; index < value.size(); index += 2U) {
      out.push_back(static_cast<std::uint8_t>(std::stoi(std::string{value.substr(index, 2U)}, nullptr, 16)));
   }
   return out;
}

std::vector<core::digest> make_leaves(std::size_t count) {
   auto leaves = std::vector<core::digest>{};
   leaves.reserve(count);
   for (auto index = std::size_t{1}; index <= count; ++index) {
      leaves.push_back(core::digest::hash("Node" + std::to_string(index)));
   }
   return leaves;
}

forge::raw::bytes encode_state(std::uint64_t mask, std::vector<core::digest> trees) {
   auto out = forge::raw::pack(mask);
   auto packed_trees = forge::raw::pack(trees);
   out.insert(out.end(), packed_trees.begin(), packed_trees.end());
   return out;
}

} // namespace

BOOST_AUTO_TEST_SUITE(forge_chain_core_merkle)

BOOST_AUTO_TEST_CASE(modern_merkle_matches_spring_golden_roots) {
   const auto leaves = make_leaves(9);
   for (auto count = std::size_t{0}; count <= leaves.size(); ++count) {
      const auto root = core::calculate_merkle_root(std::span{leaves}.first(count));
      BOOST_TEST(root.str() == modern_roots[count]);
   }
}

BOOST_AUTO_TEST_CASE(modern_merkle_preserves_left_right_order) {
   const auto leaves = make_leaves(2);
   auto reversed = leaves;
   std::ranges::reverse(reversed);

   BOOST_TEST(core::calculate_merkle_root(leaves) != core::calculate_merkle_root(reversed));
}

BOOST_AUTO_TEST_CASE(merkle_paths_cover_every_leaf_in_unbalanced_trees) {
   for (auto count = std::size_t{1}; count <= 65U; ++count) {
      const auto leaves = make_leaves(count);
      const auto root = core::calculate_merkle_root(leaves);
      for (auto index = std::size_t{0}; index < count; ++index) {
         const auto path = core::calculate_merkle_path(leaves, index);
         BOOST_TEST(core::verify_merkle_path(leaves[index], index, count, path, root));

         if (!path.empty()) {
            auto malformed = path;
            malformed.front().sibling_on_left = !malformed.front().sibling_on_left;
            BOOST_TEST(!core::verify_merkle_path(leaves[index], index, count, malformed, root));
         }
      }
   }
}

BOOST_AUTO_TEST_CASE(merkle_paths_reject_invalid_positions_and_shapes) {
   const auto leaves = make_leaves(3);
   const auto root = core::calculate_merkle_root(leaves);
   const auto path = core::calculate_merkle_path(leaves, 1U);

   BOOST_CHECK_THROW((void)core::calculate_merkle_path({}, 0U), core::exceptions::invalid_leaf_index);
   BOOST_CHECK_THROW((void)core::calculate_merkle_path(leaves, leaves.size()), core::exceptions::invalid_leaf_index);
   BOOST_TEST(!core::verify_merkle_path(leaves[1], 1U, 0U, path, root));
   BOOST_TEST(!core::verify_merkle_path(leaves[1], leaves.size(), leaves.size(), path, root));
   BOOST_TEST(!core::verify_merkle_path(leaves[1], 1U, leaves.size(), std::span{path}.first(path.size() - 1U), root));
}

BOOST_AUTO_TEST_CASE(incremental_merkle_matches_batch_root_after_every_append) {
   const auto leaves = make_leaves(1001);
   auto tree = core::incremental_merkle_tree{};

   BOOST_TEST(tree.empty());
   BOOST_TEST(tree.size() == 0U);
   BOOST_TEST(tree.root() == core::digest{});

   for (auto count = std::size_t{1}; count <= leaves.size(); ++count) {
      tree.append(leaves[count - 1U]);
      BOOST_TEST(!tree.empty());
      BOOST_TEST(tree.size() == count);
      BOOST_TEST(tree.root() == core::calculate_merkle_root(std::span{leaves}.first(count)));
   }
}

BOOST_AUTO_TEST_CASE(incremental_merkle_raw_state_matches_spring_and_can_continue) {
   const auto leaves = make_leaves(6);
   auto tree = core::incremental_merkle_tree{};
   for (auto index = std::size_t{0}; index < 5U; ++index) {
      tree.append(leaves[index]);
   }

   BOOST_TEST(forge::raw::pack(tree) == unhex(spring_state_after_five));

   auto restored = forge::raw::unpack<core::incremental_merkle_tree>(unhex(spring_state_after_five));
   restored.append(leaves[5]);
   BOOST_TEST(restored.size() == 6U);
   BOOST_TEST(restored.root() == core::calculate_merkle_root(std::span{leaves}));
}

BOOST_AUTO_TEST_CASE(incremental_merkle_rejects_malformed_raw_state) {
   const auto trees = make_leaves(1);
   BOOST_CHECK_THROW((void)forge::raw::unpack<core::incremental_merkle_tree>(encode_state(3U, trees)),
                     forge::raw::exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(incremental_merkle_append_overflow_preserves_state) {
   const auto trees = make_leaves(64);
   auto tree = forge::raw::unpack<core::incremental_merkle_tree>(
       encode_state(std::numeric_limits<std::uint64_t>::max(), trees));
   const auto root = tree.root();

   BOOST_CHECK_THROW(tree.append(core::digest::hash(std::string{"overflow"})), core::exceptions::leaf_count_overflow);
   BOOST_TEST(tree.size() == std::numeric_limits<std::uint64_t>::max());
   BOOST_TEST(tree.root() == root);
}

BOOST_AUTO_TEST_SUITE_END()
