module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module forge.chain.api.limits;

import forge.chain.api.table_key;
import forge.raw.exceptions;
import forge.raw.raw;

namespace forge::chain::api {
namespace {

void require_page(std::uint32_t value, std::uint32_t limit, std::string_view name, bool allow_zero = false) {
   if (!allow_zero && value == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API page limit must be positive",
                            forge::exceptions::ctx("field", name));
   }
   if (value > limit) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API page limit exceeds the configured maximum",
                            forge::exceptions::ctx("field", name), forge::exceptions::ctx("value", value),
                            forge::exceptions::ctx("limit", limit));
   }
}

template <typename Value> void require_packed_request(const Value& value, const protocol::service_limits& limits) {
   require_request_within_limits<Value>(value, limits);
}

bool bytes_less(const protocol::bytes& left, const protocol::bytes& right) {
   return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
}

void require_items(std::size_t count, std::uint32_t requested, const protocol::service_limits& limits,
                   std::string_view field) {
   const auto allowed = std::min<std::size_t>(requested, limits.max_page_size);
   if (count > allowed) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API response item count exceeds the request limit",
                            forge::exceptions::ctx("field", field), forge::exceptions::ctx("count", count),
                            forge::exceptions::ctx("limit", allowed));
   }
}

forge::raw::unpack_limits allocation_limits(const forge::api::core::bytes& payload,
                                            const protocol::service_limits& limits, std::uint32_t byte_limit,
                                            std::uint32_t first_container_limit = forge::raw::max_array_elements,
                                            std::optional<std::uint32_t> total_container_limit = std::nullopt) {
   const auto total_limit = total_container_limit.value_or(limits.max_container_elements);
   return forge::raw::unpack_limits{
       .max_container_elements =
           static_cast<std::uint32_t>(std::min<std::size_t>(limits.max_container_elements, payload.size())),
       .max_total_container_elements = static_cast<std::uint32_t>(std::min<std::size_t>(total_limit, payload.size())),
       .max_bytes = static_cast<std::uint32_t>(std::min<std::size_t>(byte_limit, payload.size())),
       .first_container_elements = first_container_limit,
   };
}

forge::raw::unpack_limits request_allocation_limits(std::string_view api, std::string_view method,
                                                    const forge::api::core::bytes& payload,
                                                    const protocol::service_limits& limits) {
   if (api == "forge.chain.api.submission" && method == "submit_batch") {
      return allocation_limits(payload, limits, limits.max_request_bytes, limits.max_transaction_batch_size);
   }
   if (api == "forge.chain.api.state" && method == "get_changes") {
      return allocation_limits(payload, limits, limits.max_request_bytes, limits.max_state_batch_size);
   }
   if (api == "forge.chain.api.state" && method == "get_accounts_by_authorizers") {
      return allocation_limits(payload, limits, limits.max_request_bytes, forge::raw::max_array_elements,
                               std::optional<std::uint32_t>{limits.max_state_batch_size});
   }
   return allocation_limits(payload, limits, limits.max_request_bytes);
}

forge::raw::unpack_limits response_allocation_limits(std::string_view api, std::string_view method,
                                                     const forge::api::core::bytes& payload,
                                                     const protocol::service_limits& limits) {
   if (api == "forge.chain.api.submission" && method == "submit_batch") {
      return allocation_limits(payload, limits, limits.max_response_bytes, limits.max_transaction_batch_size);
   }
   return allocation_limits(payload, limits, limits.max_response_bytes);
}

template <typename Decoder>
void decode_request(const Decoder& decoder, std::string_view api, std::string_view method,
                    const forge::api::core::bytes& payload, const protocol::service_limits& limits) {
   if (!decoder) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API method has no canonical request decoder",
                            forge::exceptions::ctx("api", api), forge::exceptions::ctx("method", method));
   }
   try {
      decoder(payload, request_allocation_limits(api, method, payload, limits));
   } catch (const forge::raw::exceptions::allocation_limit& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API request exceeds allocation limits",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API request payload is malformed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API request payload is malformed");
   }
}

template <typename Decoder>
void decode_response(const Decoder& decoder, std::string_view api, std::string_view method,
                     const forge::api::core::bytes& payload, const protocol::service_limits& limits) {
   if (!decoder) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API method has no canonical response decoder",
                            forge::exceptions::ctx("api", api), forge::exceptions::ctx("method", method));
   }
   try {
      decoder(payload, response_allocation_limits(api, method, payload, limits));
   } catch (const forge::raw::exceptions::allocation_limit& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API response exceeds allocation limits",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed response payload",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed response payload");
   }
}

template <typename Value>
Value unpack_request(const forge::api::core::bytes& payload, const protocol::service_limits& limits,
                     std::uint32_t first_container_limit = forge::raw::max_array_elements,
                     std::optional<std::uint32_t> total_container_limit = std::nullopt) {
   try {
      return forge::raw::unpack_exact<Value>(payload, allocation_limits(payload, limits, limits.max_request_bytes,
                                                                        first_container_limit, total_container_limit));
   } catch (const forge::raw::exceptions::allocation_limit& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API request exceeds allocation limits",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API request payload is malformed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API request payload is malformed");
   }
}

template <typename Value>
Value unpack_response(const forge::api::core::bytes& payload, const protocol::service_limits& limits,
                      std::uint32_t first_container_limit = forge::raw::max_array_elements) {
   try {
      return forge::raw::unpack_exact<Value>(
          payload, allocation_limits(payload, limits, limits.max_response_bytes, first_container_limit));
   } catch (const forge::raw::exceptions::allocation_limit& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API response exceeds allocation limits",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed response payload",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed response payload");
   }
}

protocol::audited_response unpack_audited_prefix(const forge::api::core::bytes& payload,
                                                 const protocol::service_limits& limits) {
   try {
      return forge::raw::unpack<protocol::audited_response>(
          payload, allocation_limits(payload, limits, limits.max_response_bytes));
   } catch (const forge::raw::exceptions::allocation_limit& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API audit response exceeds allocation limits",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed audited response",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::unavailable, "chain API owner produced a malformed audited response");
   }
}

std::optional<std::uint32_t> require_method_request(std::string_view api, std::string_view method,
                                                    const forge::api::core::bytes& payload,
                                                    const protocol::service_limits& limits) {
   if (payload.size() > limits.max_request_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API request exceeds the configured byte limit",
                            forge::exceptions::ctx("bytes", payload.size()),
                            forge::exceptions::ctx("limit", limits.max_request_bytes));
   }

   if (api == "forge.chain.api.block") {
      if (method == "get_canonical_range") {
         const auto request = unpack_request<protocol::block_range_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_activated_protocol_features") {
         const auto request = unpack_request<protocol::protocol_features_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_producers") {
         const auto request = unpack_request<protocol::producers_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
   } else if (api == "forge.chain.api.state") {
      if (method == "get_range") {
         const auto request = unpack_request<protocol::state_range_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_changes") {
         const auto request =
             unpack_request<protocol::state_changes_request>(payload, limits, limits.max_state_batch_size);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_table_rows") {
         const auto request = unpack_request<protocol::table_rows_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_table_scope") {
         const auto request = unpack_request<protocol::table_scope_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_scheduled_transactions") {
         const auto request = unpack_request<protocol::scheduled_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "get_accounts_by_authorizers") {
         const auto request =
             unpack_request<protocol::authorizers_request>(payload, limits, forge::raw::max_array_elements,
                                                           std::optional<std::uint32_t>{limits.max_state_batch_size});
         require_request_within_limits(request, limits);
         return request.limit;
      }
   } else if (api == "forge.chain.api.admin") {
      if (method == "account_ram_corrections") {
         const auto request = unpack_request<protocol::ram_corrections_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
      if (method == "unapplied_transactions") {
         const auto request = unpack_request<protocol::unapplied_transactions_request>(payload, limits);
         require_request_within_limits(request, limits);
         return request.limit;
      }
   } else if (api == "forge.chain.api.transaction" && method == "await_transaction") {
      require_request_within_limits(unpack_request<protocol::transaction_await_request>(payload, limits), limits);
   } else if (api == "forge.chain.api.submission") {
      if (method == "submit") {
         require_request_within_limits(unpack_request<protocol::transaction_submit_request>(payload, limits), limits);
      } else if (method == "submit_batch") {
         const auto request = unpack_request<protocol::transaction_submit_batch_request>(
             payload, limits, limits.max_transaction_batch_size);
         require_request_within_limits(request, limits);
         return static_cast<std::uint32_t>(request.transactions.size());
      }
   }
   return std::nullopt;
}

void require_method_response(std::string_view api, std::string_view method, bool audited_response,
                             const forge::api::core::bytes& payload,
                             const std::optional<std::uint32_t>& requested_items,
                             const protocol::service_limits& limits) {
   if (payload.size() > limits.max_response_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API response exceeds the configured byte limit",
                            forge::exceptions::ctx("bytes", payload.size()),
                            forge::exceptions::ctx("limit", limits.max_response_bytes));
   }
   if (audited_response) {
      require_response_within_limits(unpack_audited_prefix(payload, limits), limits);
   }
   if (!requested_items) {
      return;
   }

   if (api == "forge.chain.api.block") {
      if (method == "get_canonical_range") {
         require_items(unpack_response<protocol::block_range_response>(payload, limits).blocks.size(), *requested_items,
                       limits, "blocks");
      } else if (method == "get_activated_protocol_features") {
         require_items(unpack_response<protocol::protocol_features_response>(payload, limits).features.size(),
                       *requested_items, limits, "features");
      } else if (method == "get_producers") {
         require_items(unpack_response<protocol::producers_response>(payload, limits).rows.size(), *requested_items,
                       limits, "rows");
      }
   } else if (api == "forge.chain.api.state") {
      if (method == "get_range") {
         require_items(unpack_response<protocol::state_range_response>(payload, limits).rows.size(), *requested_items,
                       limits, "rows");
      } else if (method == "get_changes") {
         require_items(unpack_response<protocol::state_changes_response>(payload, limits).blocks.size(),
                       *requested_items, limits, "blocks");
      } else if (method == "get_table_rows") {
         require_items(unpack_response<protocol::table_rows_response>(payload, limits).rows.size(), *requested_items,
                       limits, "rows");
      } else if (method == "get_table_scope") {
         require_items(unpack_response<protocol::table_scope_response>(payload, limits).rows.size(), *requested_items,
                       limits, "rows");
      } else if (method == "get_scheduled_transactions") {
         require_items(unpack_response<protocol::scheduled_response>(payload, limits).transactions.size(),
                       *requested_items, limits, "transactions");
      } else if (method == "get_accounts_by_authorizers") {
         require_items(unpack_response<protocol::authorizers_response>(payload, limits).accounts.size(),
                       *requested_items, limits, "accounts");
      }
   } else if (api == "forge.chain.api.submission" && method == "submit_batch") {
      require_transaction_batch_response_within_limits(
          unpack_response<std::vector<protocol::transaction_submit_response>>(payload, limits,
                                                                              limits.max_transaction_batch_size),
          *requested_items, limits);
   } else if (api == "forge.chain.api.admin") {
      if (method == "account_ram_corrections") {
         require_items(unpack_response<protocol::ram_corrections_response>(payload, limits).rows.size(),
                       *requested_items, limits, "rows");
      } else if (method == "unapplied_transactions") {
         require_items(unpack_response<protocol::unapplied_transactions_response>(payload, limits).transactions.size(),
                       *requested_items, limits, "transactions");
      }
   }
}

} // namespace

void require_request_within_limits(const protocol::block_range_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
}

void require_request_within_limits(const protocol::protocol_features_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.lower_bound && value.upper_bound && *value.upper_bound < *value.lower_bound) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "protocol feature lower bound exceeds its upper bound");
   }
}

void require_request_within_limits(const protocol::producers_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
}

void require_request_within_limits(const protocol::state_range_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.range.lower && value.range.upper && bytes_less(*value.range.upper, *value.range.lower)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state range lower bound exceeds its upper bound");
   }
}

void require_request_within_limits(const protocol::state_changes_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.from_block > value.to_block) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes interval is reversed");
   }
   if (value.ranges.size() > limits.max_state_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "state changes range count exceeds the configured maximum",
                            forge::exceptions::ctx("count", value.ranges.size()),
                            forge::exceptions::ctx("limit", limits.max_state_batch_size));
   }
   for (const auto& range : value.ranges) {
      if (range.lower && range.upper && bytes_less(*range.upper, *range.lower)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes range lower bound exceeds its upper bound");
      }
   }
   const auto range_count = value.ranges.empty() ? std::size_t{1U} : value.ranges.size();
   if (value.cursor && (value.cursor->range >= range_count || value.cursor->block <= value.from_block ||
                        value.cursor->block > value.to_block)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes cursor is outside the requested interval");
   }
}

void require_request_within_limits(const protocol::table_rows_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
   validate_table_rows_request(value);
}

void require_request_within_limits(const protocol::table_scope_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
}

void require_request_within_limits(const protocol::scheduled_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
}

void require_request_within_limits(const protocol::authorizers_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   const auto inputs = value.accounts.size() + value.keys.size();
   if (inputs > limits.max_state_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "authorizer input count exceeds the configured maximum",
                            forge::exceptions::ctx("count", inputs),
                            forge::exceptions::ctx("limit", limits.max_state_batch_size));
   }
}

void require_request_within_limits(const protocol::transaction_submit_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   if (value.timeout_ms == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction submit timeout must be positive");
   }
   if (value.timeout_ms > limits.max_await_ms) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "transaction submit timeout exceeds the configured maximum",
                            forge::exceptions::ctx("timeout_ms", value.timeout_ms),
                            forge::exceptions::ctx("limit", limits.max_await_ms));
   }
}

void require_request_within_limits(const protocol::transaction_submit_batch_request& value,
                                   const protocol::service_limits& limits) {
   if (value.transactions.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction batch must not be empty");
   }
   if (value.transactions.size() > limits.max_transaction_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "transaction batch count exceeds the configured maximum",
                            forge::exceptions::ctx("count", value.transactions.size()),
                            forge::exceptions::ctx("limit", limits.max_transaction_batch_size));
   }
   if (value.timeout_ms == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction submit batch timeout must be positive");
   }
   if (value.timeout_ms > limits.max_await_ms) {
      FORGE_THROW_EXCEPTION(
          exceptions::resource_exhausted, "transaction submit batch timeout exceeds the configured maximum",
          forge::exceptions::ctx("timeout_ms", value.timeout_ms), forge::exceptions::ctx("limit", limits.max_await_ms));
   }
   require_packed_request(value, limits);
   for (const auto& transaction : value.transactions) {
      require_request_within_limits(transaction, limits);
   }
}

void require_request_within_limits(const protocol::transaction_await_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   if (value.timeout_ms == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction await timeout must be positive");
   }
   if (value.timeout_ms > limits.max_await_ms) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "transaction await timeout exceeds the configured maximum",
                            forge::exceptions::ctx("timeout_ms", value.timeout_ms),
                            forge::exceptions::ctx("limit", limits.max_await_ms));
   }
}

void require_request_within_limits(const protocol::ram_corrections_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
}

void require_request_within_limits(const protocol::unapplied_transactions_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit", true);
}

void require_response_within_limits(const protocol::block_range_response& response,
                                    const protocol::block_range_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.blocks.size(), request.limit, limits, "blocks");
}

void require_response_within_limits(const protocol::protocol_features_response& response,
                                    const protocol::protocol_features_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.features.size(), request.limit, limits, "features");
}

void require_response_within_limits(const protocol::producers_response& response,
                                    const protocol::producers_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.rows.size(), request.limit, limits, "rows");
}

void require_response_within_limits(const protocol::state_range_response& response,
                                    const protocol::state_range_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.rows.size(), request.limit, limits, "rows");
}

void require_response_within_limits(const protocol::state_changes_response& response,
                                    const protocol::state_changes_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.blocks.size(), request.limit, limits, "blocks");
}

void require_response_within_limits(const protocol::table_rows_response& response,
                                    const protocol::table_rows_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.rows.size(), request.limit, limits, "rows");
}

void require_response_within_limits(const protocol::table_scope_response& response,
                                    const protocol::table_scope_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.rows.size(), request.limit, limits, "rows");
}

void require_response_within_limits(const protocol::scheduled_response& response,
                                    const protocol::scheduled_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.transactions.size(), request.limit, limits, "transactions");
}

void require_response_within_limits(const protocol::authorizers_response& response,
                                    const protocol::authorizers_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.accounts.size(), request.limit, limits, "accounts");
}

void require_response_within_limits(const protocol::ram_corrections_response& response,
                                    const protocol::ram_corrections_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.rows.size(), request.limit, limits, "rows");
}

void require_response_within_limits(const protocol::unapplied_transactions_response& response,
                                    const protocol::unapplied_transactions_request& request,
                                    const protocol::service_limits& limits) {
   require_response_within_limits(response, limits);
   require_items(response.transactions.size(), request.limit, limits, "transactions");
}

void require_transaction_batch_response_within_limits(
    const std::vector<protocol::transaction_submit_response>& responses, std::size_t request_count,
    const protocol::service_limits& limits) {
   require_response_within_limits(responses, limits);
   if (responses.size() > limits.max_transaction_batch_size) {
      FORGE_THROW_EXCEPTION(
          exceptions::resource_exhausted, "chain API transaction response count exceeds the request limit",
          forge::exceptions::ctx("count", responses.size()), forge::exceptions::ctx("request_count", request_count),
          forge::exceptions::ctx("limit", limits.max_transaction_batch_size));
   }
   if (responses.size() != request_count) {
      FORGE_THROW_EXCEPTION(
          exceptions::unavailable, "chain API owner returned a transaction batch with mismatched cardinality",
          forge::exceptions::ctx("count", responses.size()), forge::exceptions::ctx("request_count", request_count));
   }
}

forge::api::core::descriptor with_service_limits(forge::api::core::descriptor value, protocol::service_limits limits) {
   const auto api = value.id.value;
   for (auto& method : value.methods) {
      auto name = method.name;
      const auto audited_response = method.has_response_trait<protocol::audited_response>();
      auto request_decoder = method.request_decoder;
      auto response_decoder = method.response_decoder;
      method.request_validator = [api, name, limits, request_decoder](const forge::api::core::bytes& payload) {
         decode_request(request_decoder, api, name, payload, limits);
         static_cast<void>(require_method_request(api, name, payload, limits));
      };
      method.response_validator = [api, name, audited_response, limits, request_decoder, response_decoder](
                                      const forge::api::core::bytes& request, const forge::api::core::bytes& response) {
         decode_request(request_decoder, api, name, request, limits);
         decode_response(response_decoder, api, name, response, limits);
         require_method_response(api, name, audited_response, response,
                                 require_method_request(api, name, request, limits), limits);
      };
   }
   return value;
}

} // namespace forge::chain::api
