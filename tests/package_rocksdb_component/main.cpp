import forge.rocksdb.store;

int main() {
   auto key = forge::rocksdb::make_key("package");
   return key.empty() ? 1 : 0;
}
