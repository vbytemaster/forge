import forge.plugins.http.server.plugin;

int main() {
   const auto descriptor = forge::plugins::http::server::descriptor();
   return descriptor.id.value == "forge.plugins.http.server" ? 0 : 1;
}
