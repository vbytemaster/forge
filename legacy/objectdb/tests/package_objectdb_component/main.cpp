import forge.objectdb.key;

int main() {
   const auto key = forge::objectdb::make_object_key({1}, {2});
   return key.empty() ? 1 : 0;
}
