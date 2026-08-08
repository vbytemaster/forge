#include <concepts>
#include <optional>
#include <variant>

import forge.chain.protocol.block;
import forge.chain.protocol.finalizer_policy;
import forge.chain.protocol.producer_authority;
import forge.chain.protocol.producer_schedule;
import forge.codec.json;
import forge.raw.codec;
import forge.variant.described;
import forge.variant.static_variant;
import forge.variant.value;

static_assert(std::same_as<decltype(forge::chain::protocol::block_header::new_producers),
                           std::optional<forge::chain::protocol::producer_schedule>>);

bool producer_authority_json_roundtrip() {
   auto schedule = forge::chain::protocol::producer_schedule{};
   auto authority_schedule = forge::chain::protocol::producer_authority_schedule{
       .version = 7U,
       .producers = {forge::chain::protocol::producer_authority{
           .producer_name = {},
           .authority =
               forge::chain::protocol::block_signing_authority_v0{
                   .threshold = 1U,
                   .keys = {forge::chain::protocol::key_weight{
                       .key = forge::chain::protocol::public_key{std::in_place_index<0>},
                       .weight = 1U,
                   }},
               },
       }},
   };
   auto finalizer_policy = forge::chain::protocol::finalizer_policy{};
   auto encoded = forge::variant{authority_schedule};
   auto decoded = forge::chain::protocol::producer_authority_schedule{};
   forge::from_variant(encoded, decoded);
   auto finalizer_encoded = forge::variant{finalizer_policy};
   auto finalizer_decoded = forge::chain::protocol::finalizer_policy{};
   forge::from_variant(finalizer_encoded, finalizer_decoded);
   (void)schedule;
   (void)authority_schedule;
   (void)finalizer_policy;
   (void)decoded;
   (void)finalizer_decoded;
   const auto json = forge::codec::json::write(authority_schedule);
   if (!json.ok()) {
      return false;
   }
   const auto exact = forge::codec::json::read<forge::chain::protocol::producer_authority_schedule>(
       json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   const auto raw = forge::raw::pack(authority_schedule);
   const auto raw_decoded = forge::raw::unpack_exact<forge::chain::protocol::producer_authority_schedule>(raw);
   return exact.ok() && exact.value == authority_schedule && raw_decoded == authority_schedule;
}
