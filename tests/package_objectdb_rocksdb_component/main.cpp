#include <concepts>

import forge.objectdb.store;
import forge.objectdb.rocksdb;

int main() {
   static_assert(std::derived_from<forge::objectdb::rocksdb::session, forge::objectdb::session>);
   auto cfg = forge::objectdb::rocksdb::config{};
   cfg.path = "unused";
   cfg.family = "objectdb";
   return cfg.family == "objectdb" ? 0 : 1;
}
