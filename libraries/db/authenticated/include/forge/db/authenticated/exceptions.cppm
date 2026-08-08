module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.db.authenticated.exceptions;

export import forge.exceptions;

export namespace forge::db::authenticated::exceptions {

enum class code : std::uint16_t {
   invalid_store = 1,
   corrupt_node = 2,
   invalid_mutation = 3,
   invalid_version = 4,
   version_unavailable = 5,
   root_mismatch = 6,
   invalid_proof = 7,
   proof_limit_exceeded = 8,
   transaction_closed = 9,
   transaction_not_staged = 10,
   prune_limit_too_small = 11,
   invalid_range = 12,
   invalid_prune = 13,
   backend_failure = 14,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.db.authenticated")

using invalid_store = forge::exceptions::coded_exception<code, code::invalid_store>;
using corrupt_node = forge::exceptions::coded_exception<code, code::corrupt_node>;
using invalid_mutation = forge::exceptions::coded_exception<code, code::invalid_mutation>;
using invalid_version = forge::exceptions::coded_exception<code, code::invalid_version>;
using version_unavailable = forge::exceptions::coded_exception<code, code::version_unavailable>;
using root_mismatch = forge::exceptions::coded_exception<code, code::root_mismatch>;
using invalid_proof = forge::exceptions::coded_exception<code, code::invalid_proof>;
using proof_limit_exceeded = forge::exceptions::coded_exception<code, code::proof_limit_exceeded>;
using transaction_closed = forge::exceptions::coded_exception<code, code::transaction_closed>;
using transaction_not_staged = forge::exceptions::coded_exception<code, code::transaction_not_staged>;
using prune_limit_too_small = forge::exceptions::coded_exception<code, code::prune_limit_too_small>;
using invalid_range = forge::exceptions::coded_exception<code, code::invalid_range>;
using invalid_prune = forge::exceptions::coded_exception<code, code::invalid_prune>;
using backend_failure = forge::exceptions::coded_exception<code, code::backend_failure>;

} // namespace forge::db::authenticated::exceptions
