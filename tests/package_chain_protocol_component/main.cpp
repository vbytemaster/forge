#include <concepts>
#include <cstdint>
#include <flat_map>
#include <optional>
#include <string>
#include <vector>

import forge.chain.protocol.action;
import forge.chain.protocol.action_receipt;
import forge.chain.protocol.block;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction;

bool producer_authority_json_roundtrip();

int main() {
   static_assert(std::same_as<forge::chain::protocol::bytes, std::vector<std::uint8_t>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_scope_request{}.cursor),
                              std::optional<forge::chain::protocol::bytes>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_scope_response{}.next),
                              std::optional<forge::chain::protocol::bytes>>);
   const auto digest = forge::chain::protocol::digest::hash(std::string{"package-chain-protocol"});
   auto transaction = forge::chain::protocol::transaction{};
   auto action = forge::chain::protocol::action{};
   auto receipt = forge::chain::protocol::action_receipt{};
   receipt.auth_sequence.emplace(forge::chain::protocol::account_name{1U}, 1U);
   receipt.act_digest = forge::chain::protocol::generate_action_digest(action, forge::chain::protocol::bytes{});
   const auto savanna_digest = forge::chain::protocol::calculate_savanna_action_digest(receipt, action);
   auto block = forge::chain::protocol::signed_block{};
   auto key = forge::chain::protocol::key256::make_from_word_sequence<forge::chain::protocol::uint128_t>(
       forge::chain::protocol::uint128_t{1U}, forge::chain::protocol::uint128_t{2U});
   block.transaction_mroot = digest;
   (void)transaction;
   (void)savanna_digest;
   (void)block;
   (void)key;
   return producer_authority_json_roundtrip() ? 0 : 1;
}
