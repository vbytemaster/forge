module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.db.object.exceptions;

export import forge.exceptions;

export namespace forge::db::object::exceptions {

enum class code : std::uint16_t {
   invalid_descriptor = 1,
   invalid_cursor = 2,
   not_found = 3,
   duplicate_object = 4,
   unregistered_object = 5,
   transaction_closed = 6,
   unsupported_operation = 7,
   invalid_index_key = 8,
   invalid_header = 9,
   incompatible_version = 10,
   aggregate_rebuild_required = 11,
   aggregate_corruption = 12,
   aggregate_overflow = 13,
   stale_precommit_projection = 14,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.db.object")

using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;
using invalid_cursor = forge::exceptions::coded_exception<code, code::invalid_cursor>;
using not_found = forge::exceptions::coded_exception<code, code::not_found>;
using duplicate_object = forge::exceptions::coded_exception<code, code::duplicate_object>;
using unregistered_object = forge::exceptions::coded_exception<code, code::unregistered_object>;
using transaction_closed = forge::exceptions::coded_exception<code, code::transaction_closed>;
using unsupported_operation = forge::exceptions::coded_exception<code, code::unsupported_operation>;
using invalid_index_key = forge::exceptions::coded_exception<code, code::invalid_index_key>;
using invalid_header = forge::exceptions::coded_exception<code, code::invalid_header>;
using incompatible_version = forge::exceptions::coded_exception<code, code::incompatible_version>;
using aggregate_rebuild_required = forge::exceptions::coded_exception<code, code::aggregate_rebuild_required>;
using aggregate_corruption = forge::exceptions::coded_exception<code, code::aggregate_corruption>;
using aggregate_overflow = forge::exceptions::coded_exception<code, code::aggregate_overflow>;
using stale_precommit_projection = forge::exceptions::coded_exception<code, code::stale_precommit_projection>;

} // namespace forge::db::object::exceptions
