module;

#include <array>
#include <cstdint>
#include <span>

export module forge.chain.api.table_key;

export import forge.chain.protocol.state_query;
export import forge.chain.protocol.fixed_key;

export namespace forge::chain::api {

[[nodiscard]] protocol::bytes encode_table_key(std::uint64_t value);
[[nodiscard]] protocol::bytes encode_table_key(protocol::uint128_t value);
[[nodiscard]] protocol::bytes encode_table_key(const protocol::key256& value);
[[nodiscard]] protocol::bytes encode_table_key(double value);
[[nodiscard]] protocol::bytes encode_table_key(std::span<const std::uint8_t, 16> ieee_binary128);

void validate_table_index(protocol::table_index index);
void validate_table_key(protocol::table_index_kind kind, std::span<const std::uint8_t> value);
void validate_table_rows_request(const protocol::table_rows_request& request);

} // namespace forge::chain::api
