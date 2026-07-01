module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

export module forge.objectdb.types;

export namespace forge::objectdb {

struct object_type {
   std::uint8_t space = 0;
   std::uint16_t type = 0;

   bool operator==(const object_type&) const = default;
   auto operator<=>(const object_type&) const = default;
};

struct index_id {
   std::uint32_t value = 0;

   bool operator==(const index_id&) const = default;
   auto operator<=>(const index_id&) const = default;
};

enum class index_kind : std::uint8_t {
   primary_unique = 1,
   secondary_unique = 2,
   secondary_non_unique = 3,
};

enum class record_kind : std::uint8_t {
   object_record = 0x10,
   secondary_unique_index = 0x20,
   secondary_non_unique_index = 0x21,
};

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

struct key_range {
   record_key begin;
   record_key end;
   record_key prefix;
   bool has_end = true;
};

struct record_entry {
   record_key key;
   std::vector<std::byte> value;
};

struct record_scan_result {
   std::vector<record_entry> entries;
   std::optional<record_key> next;
};

} // namespace forge::objectdb
