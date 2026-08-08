#pragma once

namespace forge::plugins::db::store {

struct managed_store {
   std::string name;
   std::string driver_name;
   std::optional<std::string> durability;
   std::string path;
   std::vector<std::string> families;
   store_options options;
   bool owns_driver = false;
   std::shared_ptr<forge::db::core::driver> driver;
   std::shared_ptr<forge::db::object::store> objects;
   std::shared_ptr<forge::db::blob::store> blobs;
   std::shared_ptr<forge::db::revision::store> revisions;
   bool opened = false;
   bool started = false;
};

} // namespace forge::plugins::db::store
