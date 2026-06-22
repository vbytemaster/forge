import fcl.plugins.signing.provider.plugin;

int main() {
   const auto descriptor = fcl::plugins::signing::provider::descriptor();
   return descriptor.id.value == "fcl.plugins.signing.provider" ? 0 : 1;
}
