#include <concepts>

import forge.api.transport.client;
import forge.api.transport.connection;
import forge.api.transport.server;

int main() {
   static_assert(std::derived_from<forge::api::transport::client,
                                   forge::api::core::remote_invoker>);
   return 0;
}
