module;

#include <utility>

export module forge.objectdb.record;

import forge.objectdb.types;

export namespace forge::objectdb {

enum class mutation_kind {
   put,
   erase,
};

struct record {
   byte_vector key;
   byte_vector value;
};

struct mutation {
   mutation_kind kind = mutation_kind::put;
   byte_vector key;
   byte_vector value;

   [[nodiscard]] static mutation put(byte_vector key, byte_vector value) {
      return mutation{.kind = mutation_kind::put, .key = std::move(key), .value = std::move(value)};
   }

   [[nodiscard]] static mutation erase(byte_vector key) {
      return mutation{.kind = mutation_kind::erase, .key = std::move(key), .value = {}};
   }
};

struct object_record {
   table_id table;
   object_id object;
   byte_vector value;
};

struct index_record {
   table_id table;
   index_id index;
   byte_vector encoded_value;
   object_id object;
};

} // namespace forge::objectdb
