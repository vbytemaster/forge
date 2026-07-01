#include <cstdint>
#include <string>

import forge.ids.types;
import forge.objectdb.descriptor;
import forge.objectdb.layout;

struct account {
   forge::ids::typed_id<1, 7> id;
   std::string name;
};

struct by_id;
struct by_name;

using account_object = forge::objectdb::object<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id, &account::id>,
      forge::objectdb::secondary_unique<by_name, &account::name>>>;

int main() {
   static_assert(forge::objectdb::object_model<account_object>);
   constexpr auto type = forge::objectdb::object_type_of<account_object>::value;
   const auto key = forge::objectdb::layout<account_object>::object_record_key(forge::ids::typed_id<1, 7>{42});
   return type.space == 1 && type.type == 7 && !key.empty() ? 0 : 1;
}
