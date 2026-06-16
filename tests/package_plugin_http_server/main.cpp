import fcl.plugins.http_server.plugin;

int main() {
   const auto descriptor = fcl::plugins::http_server::descriptor();
   return descriptor.id.value == "fcl.http_server" ? 0 : 1;
}
