#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import forge.db.authenticated.exceptions;
import forge.db.authenticated.hash;
import forge.db.authenticated.proof;
import forge.db.authenticated.types;

namespace authenticated = forge::db::authenticated;

namespace {

authenticated::bytes bytes(std::string value) {
   return {
       reinterpret_cast<const std::byte*>(value.data()),
       reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

authenticated::proof_leaf leaf(std::string key, std::string value) {
   auto encoded_value = bytes(std::move(value));
   return {
       .key = bytes(std::move(key)),
       .value_hash = authenticated::hash_value(encoded_value),
       .value = std::move(encoded_value),
   };
}

struct ranked_fixture {
   std::string domain;
   std::string tree_domain;
   authenticated::proof_leaf a;
   authenticated::proof_leaf b;
   authenticated::proof_leaf c;
   authenticated::proof_leaf d;
   authenticated::proof_branch left;
   authenticated::proof_branch right;
   authenticated::root anchor;
};

authenticated::digest leaf_hash(const ranked_fixture& fixture, const authenticated::proof_leaf& value) {
   return authenticated::hash_leaf(fixture.tree_domain, value.key, value.value_hash);
}

authenticated::digest branch_hash(const ranked_fixture& fixture, const authenticated::proof_branch& value) {
   return authenticated::hash_inner(fixture.tree_domain, value.height, value.size, value.min_key, value.max_key,
                                    value.separator, value.left_hash, value.right_hash);
}

ranked_fixture make_ranked_fixture() {
   auto fixture = ranked_fixture{
       .domain = "forge.test.authenticated.ranked-adversarial.v3",
       .a = leaf("a", "one"),
       .b = leaf("b", "two"),
       .c = leaf("c", "three"),
       .d = leaf("d", "four"),
   };
   fixture.tree_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::state);
   fixture.left = {
       .height = 1,
       .size = 2,
       .min_key = fixture.a.key,
       .max_key = fixture.b.key,
       .separator = fixture.b.key,
       .left_hash = leaf_hash(fixture, fixture.a),
       .right_hash = leaf_hash(fixture, fixture.b),
   };
   fixture.right = {
       .height = 1,
       .size = 2,
       .min_key = fixture.c.key,
       .max_key = fixture.d.key,
       .separator = fixture.d.key,
       .left_hash = leaf_hash(fixture, fixture.c),
       .right_hash = leaf_hash(fixture, fixture.d),
   };
   fixture.anchor = {
       .state_root = authenticated::hash_inner(fixture.tree_domain, 2, 4, fixture.a.key, fixture.d.key, fixture.c.key,
                                               branch_hash(fixture, fixture.left), branch_hash(fixture, fixture.right)),
       .state_size = 4,
   };
   return fixture;
}

authenticated::range_proof full_proof(const ranked_fixture& fixture, authenticated::range_request request) {
   return {
       .anchor = fixture.anchor,
       .request = std::move(request),
       .nodes =
           {
               authenticated::range_inner{
                   .height = 2,
                   .size = 4,
                   .min_key = fixture.a.key,
                   .max_key = fixture.d.key,
                   .separator = fixture.c.key,
               },
               authenticated::range_inner{
                   .height = fixture.left.height,
                   .size = fixture.left.size,
                   .min_key = fixture.left.min_key,
                   .max_key = fixture.left.max_key,
                   .separator = fixture.left.separator,
               },
               fixture.a,
               fixture.b,
               authenticated::range_inner{
                   .height = fixture.right.height,
                   .size = fixture.right.size,
                   .min_key = fixture.right.min_key,
                   .max_key = fixture.right.max_key,
                   .separator = fixture.right.separator,
               },
               fixture.c,
               fixture.d,
           },
   };
}

void require_invalid_range(const ranked_fixture& fixture, const authenticated::range_proof& proof) {
   BOOST_CHECK_THROW(
       static_cast<void>(authenticated::verify_range(fixture.domain, fixture.anchor, proof.request, proof.tree, proof)),
       authenticated::exceptions::invalid_proof);
}

void collapse_left_subtree(authenticated::range_proof& proof, const authenticated::proof_branch& branch) {
   proof.nodes.erase(proof.nodes.begin() + 1, proof.nodes.begin() + 4);
   proof.nodes.insert(proof.nodes.begin() + 1, branch);
}

void collapse_right_subtree(authenticated::range_proof& proof, const authenticated::proof_branch& branch) {
   proof.nodes.erase(proof.nodes.begin() + 4, proof.nodes.begin() + 7);
   proof.nodes.insert(proof.nodes.begin() + 4, branch);
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_authenticated_proof_adversarial_test_suite)

BOOST_AUTO_TEST_CASE(forge_v3_point_vectors_match_cross_language_membership_and_nonmembership) {
   constexpr auto domain = std::string_view{"forge.test.cross-language.point.v3"};
   const auto tree_domain = authenticated::canonical_tree_domain(domain, authenticated::proof_tree::state);
   const auto alpha = leaf("alpha", "one");
   const auto gamma = leaf("gamma", "three");

   BOOST_TEST(alpha.value_hash.str() == "f91eb855f01d5bb7bf379141735bb57f1ffc66b1f94bc9988f10a7b91fb80bb7");
   BOOST_TEST(gamma.value_hash.str() == "fa3435e1ca13ee8e6077d37652e69910d95aa2bdd473fe59c99db7097b756008");

   const auto anchor = authenticated::root{
       .state_root = authenticated::hash_inner(tree_domain, 1, 2, alpha.key, gamma.key, gamma.key,
                                               authenticated::hash_leaf(tree_domain, alpha.key, alpha.value_hash),
                                               authenticated::hash_leaf(tree_domain, gamma.key, gamma.value_hash)),
       .state_size = 2,
   };
   BOOST_TEST(anchor.state_root.str() == "a9d7cc0f76024d939d11d752016e985635a4f861f4e51a02484906730d01799d");

   const auto path = std::vector<authenticated::proof_step>{{
       .child = authenticated::branch_side::left,
       .height = 1,
       .subtree_size = 2,
       .min_key = alpha.key,
       .max_key = gamma.key,
       .separator = gamma.key,
       .sibling = gamma,
   }};
   const auto membership = authenticated::point_proof{
       .anchor = anchor,
       .key = alpha.key,
       .terminal = alpha,
       .path = path,
   };
   const auto verified_membership = authenticated::verify_point(domain, anchor, alpha.key, membership);
   BOOST_TEST(verified_membership.exists);
   BOOST_TEST(verified_membership.rank == 0U);
   BOOST_REQUIRE(verified_membership.value_hash.has_value());
   BOOST_TEST(verified_membership.value_hash->str() == alpha.value_hash.str());
   BOOST_REQUIRE(verified_membership.value.has_value());
   BOOST_CHECK(*verified_membership.value == bytes("one"));

   const auto beta = bytes("beta");
   const auto nonmembership = authenticated::point_proof{
       .anchor = anchor,
       .key = beta,
       .terminal = alpha,
       .path = path,
   };
   const auto verified_nonmembership = authenticated::verify_point(domain, anchor, beta, nonmembership);
   BOOST_TEST(!verified_nonmembership.exists);
   BOOST_TEST(verified_nonmembership.rank == 1U);
   BOOST_TEST(!verified_nonmembership.value_hash.has_value());
   BOOST_TEST(!verified_nonmembership.value.has_value());
}

BOOST_AUTO_TEST_CASE(ranked_range_rejects_malformed_omitted_duplicated_and_reordered_nodes) {
   const auto fixture = make_ranked_fixture();
   const auto request = authenticated::range_request{
       .lower = fixture.b.key,
       .upper = fixture.d.key,
       .limit = 2,
       .include_values = false,
   };
   const auto valid = full_proof(fixture, request);
   const auto verified = authenticated::verify_range(fixture.domain, fixture.anchor, request, valid.tree, valid);
   BOOST_REQUIRE(verified.items.size() == 2U);
   BOOST_CHECK(verified.items[0].key == fixture.b.key);
   BOOST_TEST(verified.items[0].rank == 1U);
   BOOST_CHECK(verified.items[1].key == fixture.c.key);
   BOOST_TEST(verified.items[1].rank == 2U);

   auto malformed = valid;
   ++std::get<authenticated::range_inner>(malformed.nodes.front()).size;
   require_invalid_range(fixture, malformed);

   auto omitted = valid;
   omitted.nodes.erase(omitted.nodes.begin() + 5);
   require_invalid_range(fixture, omitted);

   auto duplicated = valid;
   duplicated.nodes.insert(duplicated.nodes.begin() + 4, duplicated.nodes[3]);
   require_invalid_range(fixture, duplicated);

   auto reordered = valid;
   std::swap(reordered.nodes[2], reordered.nodes[3]);
   require_invalid_range(fixture, reordered);
}

BOOST_AUTO_TEST_CASE(ranked_range_rejects_omitted_lower_and_upper_boundary_witnesses) {
   const auto fixture = make_ranked_fixture();

   auto missing_lower_predecessor = full_proof(fixture, {
                                                            .lower = fixture.c.key,
                                                            .limit = 1,
                                                            .include_values = false,
                                                        });
   collapse_left_subtree(missing_lower_predecessor, fixture.left);
   require_invalid_range(fixture, missing_lower_predecessor);

   auto missing_upper_boundary = full_proof(fixture, {
                                                         .lower = fixture.b.key,
                                                         .upper = fixture.c.key,
                                                         .limit = 1,
                                                         .include_values = false,
                                                     });
   collapse_right_subtree(missing_upper_boundary, fixture.right);
   require_invalid_range(fixture, missing_upper_boundary);
}

BOOST_AUTO_TEST_SUITE_END()
