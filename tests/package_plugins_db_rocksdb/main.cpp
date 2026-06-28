import forge.plugins.db.rocksdb.plugin;

int main() {
   const auto descriptor = forge::plugins::db::rocksdb::descriptor();
   return descriptor.id.value == "forge.plugins.db.rocksdb" ? 0 : 1;
}
