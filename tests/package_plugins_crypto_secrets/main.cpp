import fcl.plugins.crypto.secrets.plugin;

int main() {
   const auto descriptor = fcl::plugins::crypto::secrets::descriptor();
   return descriptor.id.value == "fcl.plugins.crypto.secrets" ? 0 : 1;
}
