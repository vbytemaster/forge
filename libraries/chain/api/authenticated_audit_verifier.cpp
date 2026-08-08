module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module forge.chain.api.authenticated_audit_verifier;

import forge.chain.api.exceptions;
import forge.chain.core.merkle;
import forge.db.authenticated.codec;
import forge.db.authenticated.hash;
import forge.db.authenticated.proof;
import forge.raw.raw;

namespace forge::chain::api {
namespace {

template <typename Function> void verify_finality_delegate(Function&& function) {
   try {
      std::forward<Function>(function)();
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "finality verifier failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "finality verifier failed");
   }
}

constexpr auto point_scheme = std::string_view{"forge.db.authenticated.point"};
constexpr auto range_scheme = std::string_view{"forge.db.authenticated.range"};
constexpr auto changes_scheme = std::string_view{"forge.db.authenticated.changes"};
constexpr auto proof_version = std::uint32_t{1};

forge::db::authenticated::bytes db_bytes(const protocol::bytes& value) {
   const auto* first = reinterpret_cast<const std::byte*>(value.data());
   return {first, first + value.size()};
}

std::optional<forge::db::authenticated::bytes> db_bytes(const std::optional<protocol::bytes>& value) {
   return value ? std::optional{db_bytes(*value)} : std::nullopt;
}

protocol::bytes protocol_bytes(const forge::db::authenticated::bytes& value) {
   const auto* first = reinterpret_cast<const std::uint8_t*>(value.data());
   return {first, first + value.size()};
}

std::optional<protocol::bytes> protocol_bytes(const std::optional<forge::db::authenticated::bytes>& value) {
   return value ? std::optional{protocol_bytes(*value)} : std::nullopt;
}

std::span<const std::byte> payload(const protocol::proof_blob& proof, std::string_view scheme) {
   if (proof.scheme != scheme || proof.version != proof_version) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API proof uses an unsupported scheme",
                            forge::exceptions::ctx("scheme", proof.scheme),
                            forge::exceptions::ctx("version", proof.version));
   }
   return {reinterpret_cast<const std::byte*>(proof.payload.data()), proof.payload.size()};
}

forge::db::authenticated::root root(const protocol::state_anchor& value) {
   return {
       .version = value.block_num,
       .state_root = value.state_root,
       .state_size = value.state_size,
       .change_root = value.change_root,
       .change_count = value.change_count,
   };
}

forge::db::authenticated::range_request range_request(const protocol::key_range& range, std::uint32_t limit,
                                                      bool reverse = false) {
   return {
       .lower = db_bytes(range.lower),
       .upper = db_bytes(range.upper),
       .limit = limit,
       .include_values = true,
       .reverse = reverse,
   };
}

void reject_state(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, message);
}

template <typename Function> decltype(auto) translate_state_error(Function&& function) {
   try {
      return std::forward<Function>(function)();
   } catch (const exceptions::invalid_state_proof&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API authenticated proof is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_state_proof, "chain API authenticated proof is invalid");
   }
}

protocol::digest receipt_transaction_id(const protocol::transaction_receipt& receipt) {
   if (const auto* id = std::get_if<protocol::transaction_id>(&receipt.trx)) {
      return *id;
   }
   if (const auto* transaction = std::get_if<protocol::packed_transaction>(&receipt.trx)) {
      return transaction->id();
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                         "transaction receipt contains no canonical transaction identity");
}

} // namespace

authenticated_audit_verifier::authenticated_audit_verifier(authenticated_audit_options options,
                                                           std::shared_ptr<finality_verifier> finality)
    : options_{std::move(options)}, finality_{std::move(finality)} {
   if (options_.chain == protocol::chain_id{} || options_.state_domain.empty() || !finality_) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required,
                            "authenticated chain API verifier requires a chain, state domain, and finality verifier");
   }
}

std::optional<protocol::block_id> authenticated_audit_verifier::preferred_finality_anchor() const {
   try {
      return finality_->preferred_trust_anchor();
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::anchor_unavailable, "finality trust anchor provider failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::anchor_unavailable, "finality trust anchor provider failed");
   }
}

void authenticated_audit_verifier::verify_context(const protocol::response_context& context) {
   if (context.chain != options_.chain || (context.anchor && context.anchor->chain != options_.chain)) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "chain API response belongs to another chain");
   }
}

void authenticated_audit_verifier::verify_finality(const protocol::state_anchor& anchor,
                                                   const protocol::proof_blob& proof) {
   if (anchor.chain != options_.chain) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "chain API anchor belongs to another chain");
   }
   verify_finality_delegate([&] { finality_->verify(anchor, proof); });
}

void authenticated_audit_verifier::verify_ancestry(const protocol::state_anchor& finalized,
                                                   std::span<const protocol::state_anchor> intermediate,
                                                   const protocol::proof_blob& proof) {
   if (finalized.chain != options_.chain ||
       std::ranges::any_of(intermediate, [&](const auto& anchor) { return anchor.chain != options_.chain; })) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "chain API ancestry belongs to another chain");
   }
   verify_finality_delegate([&] { finality_->verify_ancestry(finalized, intermediate, proof); });
}

std::optional<protocol::bytes>
authenticated_audit_verifier::verify_state_point(const protocol::state_anchor& anchor,
                                                 const protocol::state_point_request& request,
                                                 const protocol::proof_blob& proof) {
   return translate_state_error([&] {
      const auto decoded = forge::db::authenticated::decode_point(payload(proof, point_scheme), options_.proof_limits);
      const auto key = db_bytes(request.key);
      const auto verified = forge::db::authenticated::verify_point(options_.state_domain, root(anchor), key, decoded,
                                                                   options_.proof_limits);
      if (verified.exists && !verified.value) {
         reject_state("chain API point proof omitted an existing value");
      }
      return protocol_bytes(verified.value);
   });
}

protocol::state_range_response
authenticated_audit_verifier::verify_state_range(const protocol::state_anchor& anchor,
                                                 const protocol::state_range_request& request,
                                                 const protocol::proof_blob& proof) {
   return translate_state_error([&] {
      const auto decoded = forge::db::authenticated::decode_range(payload(proof, range_scheme), options_.proof_limits);
      const auto expected = range_request(request.range, request.limit, request.reverse);
      const auto verified = forge::db::authenticated::verify_range(options_.state_domain, root(anchor), expected,
                                                                   forge::db::authenticated::proof_tree::state, decoded,
                                                                   options_.proof_limits);
      auto result = protocol::state_range_response{};
      result.next_key = protocol_bytes(verified.next_key);
      result.rows.reserve(verified.items.size());
      for (const auto& item : verified.items) {
         if (!item.value) {
            reject_state("chain API range proof omitted a row value");
         }
         result.rows.push_back({.key = protocol_bytes(item.key), .value = protocol_bytes(*item.value)});
      }
      return result;
   });
}

protocol::state_change_range authenticated_audit_verifier::verify_state_changes(const protocol::state_anchor& anchor,
                                                                                const protocol::key_range& range,
                                                                                std::uint32_t limit,
                                                                                const protocol::proof_blob& proof) {
   return translate_state_error([&] {
      const auto decoded =
          forge::db::authenticated::decode_range(payload(proof, changes_scheme), options_.proof_limits);
      const auto expected = range_request(range, limit);
      const auto verified = forge::db::authenticated::verify_range(options_.state_domain, root(anchor), expected,
                                                                   forge::db::authenticated::proof_tree::changes,
                                                                   decoded, options_.proof_limits);
      auto result = protocol::state_change_range{.range = range, .next_key = protocol_bytes(verified.next_key)};
      result.mutations.reserve(verified.items.size());
      for (const auto& item : verified.items) {
         if (!item.value) {
            reject_state("chain API change proof omitted a mutation value");
         }
         result.mutations.push_back({
             .key = protocol_bytes(item.key),
             .value = protocol_bytes(forge::db::authenticated::decode_change_value(*item.value)),
         });
      }
      return result;
   });
}

void authenticated_audit_verifier::verify_transaction(const protocol::state_anchor& anchor,
                                                      const protocol::transaction_id& expected,
                                                      const protocol::transaction_status_response& response,
                                                      const protocol::transaction_inclusion_proof& proof) {
   if (response.trace || !response.block || !response.block_num || *response.block != anchor.block ||
       *response.block_num != anchor.block_num || !response.receipt || response.id != expected ||
       receipt_transaction_id(*response.receipt) != expected || response.receipt->digest() != proof.leaf) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "transaction result does not match its finalized block or receipt");
   }

   if (!forge::chain::core::verify_merkle_path(proof.leaf, proof.index, proof.leaf_count, proof.path,
                                               anchor.transaction_root)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_transaction_proof,
                            "transaction inclusion proof does not reconstruct the finalized root");
   }
}

} // namespace forge::chain::api
