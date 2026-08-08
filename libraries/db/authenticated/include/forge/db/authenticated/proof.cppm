module;

#include <span>
#include <string_view>

export module forge.db.authenticated.proof;

import forge.db.authenticated.types;

export namespace forge::db::authenticated {

[[nodiscard]] verified_point verify_point(std::string_view domain, const root& expected_anchor,
                                          std::span<const std::byte> expected_key, const point_proof& proof,
                                          const limits& settings = {});

[[nodiscard]] verified_range verify_range(std::string_view domain, const root& expected_anchor,
                                          const range_request& expected_request, proof_tree expected_tree,
                                          const range_proof& proof, const limits& settings = {});

} // namespace forge::db::authenticated
