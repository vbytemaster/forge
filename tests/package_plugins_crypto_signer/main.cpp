import fcl.plugins.crypto.signer.plugin;

int main() {
   const auto descriptor = fcl::plugins::crypto::signer::descriptor();
   return descriptor.id.value == "fcl.plugins.crypto.signer" ? 0 : 1;
}
