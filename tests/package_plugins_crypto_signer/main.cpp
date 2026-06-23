import forge.plugins.crypto.signer.plugin;

int main() {
   const auto descriptor = forge::plugins::crypto::signer::descriptor();
   return descriptor.id.value == "forge.plugins.crypto.signer" ? 0 : 1;
}
