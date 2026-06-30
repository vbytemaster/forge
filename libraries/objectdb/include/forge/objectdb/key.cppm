module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module forge.objectdb.key;

import forge.objectdb.types;

export namespace forge::objectdb {

enum class key_domain : std::uint8_t {
   object = 0x10,
   index = 0x20,
   sequence = 0x30,
};

struct key_range {
   byte_vector begin;
   byte_vector end;
   bool has_end = false;
};

class key_builder {
 public:
   key_builder() = default;
   explicit key_builder(key_domain domain);

   key_builder& append_u8(std::uint8_t value);
   key_builder& append_u32(std::uint32_t value);
   key_builder& append_u64(std::uint64_t value);
   key_builder& append_i64(std::int64_t value);
   key_builder& append_ordered_bytes(std::span<const std::byte> value);
   key_builder& append_ordered_text(std::string_view value);
   key_builder& append_raw(std::span<const std::byte> value);

   [[nodiscard]] const byte_vector& bytes() const noexcept;
   [[nodiscard]] byte_vector finish();

 private:
   byte_vector _bytes;
};

[[nodiscard]] byte_vector make_object_key(table_id table, object_id object);
[[nodiscard]] byte_vector make_object_prefix(table_id table);
[[nodiscard]] byte_vector make_index_prefix(table_id table, index_id index);
[[nodiscard]] byte_vector make_secondary_index_key(table_id table, index_id index, std::span<const std::byte> encoded_value,
                                                   object_id object);
[[nodiscard]] byte_vector make_sequence_key(table_id table);

[[nodiscard]] key_range prefix_range(std::span<const std::byte> prefix);
[[nodiscard]] bool starts_with(std::span<const std::byte> value, std::span<const std::byte> prefix) noexcept;
[[nodiscard]] std::uint64_t read_u64(std::span<const std::byte> value);

} // namespace forge::objectdb
