#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/describe.hpp>
#include <forge/objectdb/macros.hpp>

import forge.ids.types;
import forge.objectdb.descriptor;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.record;
import forge.objectdb.session;
import forge.objectdb.store;

struct account : forge::objectdb::object<account, 1, 7> {
   std::string name;
};

BOOST_DESCRIBE_STRUCT(account, (forge::objectdb::object<account, 1, 7>), (name))

struct by_id;
struct by_name;

using account_object = forge::objectdb::object_index<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id>,
      forge::objectdb::secondary_unique<by_name, &account::name>>>;

FORGE_OBJECTDB_OBJECT(account_object)

int main() {
   static_assert(forge::objectdb::object_model<account_object>);
   static_assert(std::same_as<forge::objectdb::object_index_for_id_t<account::id_type>, account_object>);
   constexpr auto type = forge::objectdb::object_id_of<account_object>::value;
   const auto key = forge::objectdb::record_key{std::vector<std::byte>{std::byte{0x01}}};
   return type.space == 1 && type.type == 7 && !key.empty() ? 0 : 1;
}
