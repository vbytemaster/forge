#include <string>

import fcl.env;

int main() {
   const auto name = fcl::env::variable_name("http", "bind-port", fcl::env::write_options{.prefix = "FCL"});
   return name == "FCL_HTTP_BIND_PORT" ? 0 : 1;
}
