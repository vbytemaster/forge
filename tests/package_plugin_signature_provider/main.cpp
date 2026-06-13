import fcl.plugins.signature_provider.plugin;

int main() {
   const auto descriptor = fcl::plugins::signature_provider::descriptor();
   return descriptor.id.value == "fcl.signature_provider" ? 0 : 1;
}
