module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

export module forge.db.authenticated.hash;

import forge.db.authenticated.types;

export namespace forge::db::authenticated {

inline constexpr auto hash_schema_version = std::uint32_t{3};

[[nodiscard]] std::string canonical_tree_domain(std::string_view domain, proof_tree tree);
[[nodiscard]] digest hash_value(std::span<const std::byte> value);
[[nodiscard]] digest hash_leaf(std::string_view domain, std::span<const std::byte> key, const digest& value_hash);
[[nodiscard]] digest hash_inner(std::string_view domain, std::uint16_t height, std::uint64_t subtree_size,
                                std::span<const std::byte> min_key, std::span<const std::byte> max_key,
                                std::span<const std::byte> separator, const digest& left, const digest& right);
[[nodiscard]] digest hash_empty(std::string_view domain);
[[nodiscard]] bytes encode_change_value(const mutation& value);
[[nodiscard]] std::optional<bytes> decode_change_value(std::span<const std::byte> value);
[[nodiscard]] digest hash_mutations(std::span<const mutation> mutations);

} // namespace forge::db::authenticated
