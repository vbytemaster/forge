module;

#include <cstddef>
#include <cstdint>
#include <span>

export module forge.objectdb.cursor;

import forge.objectdb.types;

export namespace forge::objectdb {

struct page_cursor {
   byte_vector key;

   [[nodiscard]] bool empty() const noexcept;
};

struct page_request {
   page_cursor cursor;
   std::uint64_t limit = 100;
};

struct page_window {
   byte_vector start_after;
   std::uint64_t limit = 100;
};

[[nodiscard]] page_cursor make_cursor(std::span<const std::byte> last_key);
[[nodiscard]] page_window make_page_window(const page_request& request);
void validate_page_limit(std::uint64_t limit);

} // namespace forge::objectdb
