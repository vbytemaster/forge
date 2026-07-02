module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

module forge.objectdb.cursor;

import forge.exceptions;
import forge.objectdb.exceptions;
import forge.objectdb.types;

namespace forge::objectdb {

bool page_cursor::empty() const noexcept {
   return key.empty();
}

page_cursor make_cursor(std::span<const std::byte> last_key) {
   return page_cursor{.key = byte_vector{last_key.begin(), last_key.end()}};
}

page_window make_page_window(const page_request& request) {
   validate_page_limit(request.limit);
   return page_window{.start_after = request.cursor.key, .limit = request.limit};
}

void validate_page_limit(std::uint64_t limit) {
   if (limit == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_cursor, "objectdb page limit must be greater than zero",
                            forge::exceptions::ctx("limit", limit));
   }
}

} // namespace forge::objectdb
