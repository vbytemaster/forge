import fcl.plugins.secret_provider.plugin;

int main() {
   const auto descriptor = fcl::plugins::secret_provider::descriptor();
   return descriptor.id.value == "fcl.secret_provider" ? 0 : 1;
}
