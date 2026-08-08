module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>
#include <type_traits>

export module forge.chain.api.exceptions;

import forge.api.core.descriptor;

export import forge.exceptions;
export import forge.chain.protocol.audit;

export namespace forge::chain::api::exceptions {

enum class code : std::uint16_t {
   invalid_request = 1,
   audit_not_supported = 2,
   anchor_unavailable = 3,
   wrong_chain = 4,
   invalid_finality = 5,
   invalid_state_proof = 6,
   invalid_transaction_proof = 7,
   trust_required = 8,
   history_lost = 9,
   deadline_exceeded = 10,
   unavailable = 11,
   resource_exhausted = 12,
   conflict = 13,
   admission_rejected = 14,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.api")

using invalid_request = forge::exceptions::coded_exception<code, code::invalid_request>;
using audit_not_supported = forge::exceptions::coded_exception<code, code::audit_not_supported>;
using anchor_unavailable = forge::exceptions::coded_exception<code, code::anchor_unavailable>;
using wrong_chain = forge::exceptions::coded_exception<code, code::wrong_chain>;
using invalid_finality = forge::exceptions::coded_exception<code, code::invalid_finality>;
using invalid_state_proof = forge::exceptions::coded_exception<code, code::invalid_state_proof>;
using invalid_transaction_proof = forge::exceptions::coded_exception<code, code::invalid_transaction_proof>;
using trust_required = forge::exceptions::coded_exception<code, code::trust_required>;
using history_lost = forge::exceptions::coded_exception<code, code::history_lost>;
using deadline_exceeded = forge::exceptions::coded_exception<code, code::deadline_exceeded>;
using unavailable = forge::exceptions::coded_exception<code, code::unavailable>;
using resource_exhausted = forge::exceptions::coded_exception<code, code::resource_exhausted>;
using conflict = forge::exceptions::coded_exception<code, code::conflict>;
using admission_rejected = forge::exceptions::coded_exception<code, code::admission_rejected>;

namespace descriptor {

template <typename Builder> void declare_common(Builder& method) {
   method.template error<invalid_request>(
       "invalid_request", {.status_code = forge::api::core::status::invalid_argument, .retryable = false});
   method.template error<unavailable>("unavailable",
                                      {.status_code = forge::api::core::status::unavailable, .retryable = true});
   method.template error<resource_exhausted>(
       "resource_exhausted", {.status_code = forge::api::core::status::resource_exhausted, .retryable = false});
}

template <auto Method, typename Builder> void declare_audited_query(Builder& method) {
   using response_type = forge::api::core::method_response_t<Method>;
   static_assert(std::is_base_of_v<forge::chain::protocol::audited_response, response_type>);
   method.template response_trait<forge::chain::protocol::audited_response>();
   declare_common(method);
   method.template error<audit_not_supported>(
       "audit_not_supported", {.status_code = forge::api::core::status::failed_precondition, .retryable = false});
   method.template error<anchor_unavailable>("anchor_unavailable",
                                             {.status_code = forge::api::core::status::not_found, .retryable = false});
}

template <auto Method, typename Builder> void declare_historical_query(Builder& method) {
   declare_audited_query<Method>(method);
   method.template error<history_lost>(
       "history_lost", {.status_code = forge::api::core::status::failed_precondition, .retryable = false});
}

template <typename Builder> void declare_deadline(Builder& method) {
   method.template error<deadline_exceeded>(
       "deadline_exceeded", {.status_code = forge::api::core::status::deadline_exceeded, .retryable = true});
}

template <typename Builder> void declare_mutation(Builder& method) {
   method.template error<conflict>("conflict", {.status_code = forge::api::core::status::conflict, .retryable = true});
   method.template error<admission_rejected>(
       "admission_rejected", {.status_code = forge::api::core::status::failed_precondition, .retryable = false});
}

} // namespace descriptor

} // namespace forge::chain::api::exceptions
