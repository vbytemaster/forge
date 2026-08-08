module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <flat_map>

export module forge.chain.protocol.action_receipt;

export import forge.chain.protocol.action;
export import forge.raw.varint;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain::protocol {

struct action_receipt {
   account_name receiver;
   core::digest act_digest;
   std::uint64_t global_sequence = 0;
   std::uint64_t recv_sequence = 0;
   std::flat_map<account_name, std::uint64_t> auth_sequence;
   forge::unsigned_int code_sequence = 0;
   forge::unsigned_int abi_sequence = 0;

   bool operator==(const action_receipt&) const = default;
};

core::digest calculate_savanna_witness_hash(const action_receipt& receipt);
core::digest calculate_savanna_action_digest(const action_receipt& receipt, const action& executed_action);

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(action_receipt, (),
                      (receiver, act_digest, global_sequence, recv_sequence, auth_sequence, code_sequence,
                       abi_sequence))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::action_receipt)
