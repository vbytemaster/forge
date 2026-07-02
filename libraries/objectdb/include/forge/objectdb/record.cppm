module;

#include <compare>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

export module forge.objectdb.record;

export namespace forge::objectdb {

class record_key {
 public:
   record_key() = default;

   explicit record_key(std::vector<std::byte> bytes) : _bytes(std::move(bytes)) {}

   [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept {
      return _bytes;
   }

   [[nodiscard]] bool empty() const noexcept {
      return _bytes.empty();
   }

   friend bool operator==(const record_key&, const record_key&) = default;
   friend auto operator<=>(const record_key&, const record_key&) = default;

 private:
   std::vector<std::byte> _bytes;
};

struct record_range {
   record_key begin;
   record_key end;
   record_key prefix;
   bool has_end = true;
};

struct record_entry {
   record_key key;
   std::vector<std::byte> value;
};

struct record_page {
   std::vector<record_entry> entries;
   std::optional<record_key> next;
};

} // namespace forge::objectdb
