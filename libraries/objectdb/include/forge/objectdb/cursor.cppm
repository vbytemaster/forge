module;

#include <cstdint>
#include <optional>
#include <forge/exceptions/macros.hpp>

export module forge.objectdb.cursor;

import forge.objectdb.exceptions;
import forge.objectdb.types;

export namespace forge::objectdb {

inline constexpr std::uint32_t default_page_limit = 100;
inline constexpr std::uint32_t max_page_limit = 10'000;

struct cursor {
   record_key boundary;

   bool operator==(const cursor&) const = default;
};

struct page_request {
   std::optional<cursor> after;
   std::uint32_t limit = default_page_limit;
};

inline void validate_page_request(const page_request& request) {
   if (request.limit == 0 || request.limit > max_page_limit) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "invalid objectdb page limit",
                            forge::exceptions::ctx("limit", request.limit),
                            forge::exceptions::ctx("max", max_page_limit));
   }
}

} // namespace forge::objectdb
