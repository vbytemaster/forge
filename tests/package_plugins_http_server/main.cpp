import fcl.plugins.http.server.plugin;

int main() {
   const auto descriptor = fcl::plugins::http::server::descriptor();
   return descriptor.id.value == "fcl.plugins.http.server" ? 0 : 1;
}
