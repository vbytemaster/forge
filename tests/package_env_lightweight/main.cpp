#include <string>

import forge.env;

int main() {
   const auto name = forge::env::variable_name("http", "bind-port", forge::env::write_options{.prefix = "FORGE"});
   return name == "FORGE_HTTP_BIND_PORT" ? 0 : 1;
}
