import fcl.plugins.secret.provider.plugin;

int main() {
   const auto descriptor = fcl::plugins::secret::provider::descriptor();
   return descriptor.id.value == "fcl.plugins.secret.provider" ? 0 : 1;
}
