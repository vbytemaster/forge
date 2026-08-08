#include <type_traits>

import forge.api.websocket.binding;
import forge.api.websocket.connection;
import forge.api.websocket.stream;

int main() {
   auto builder = forge::api::websocket::api();
   const auto capabilities = forge::api::websocket::binding_capabilities();
   static_assert(std::is_base_of_v<forge::api::core::connection,
                                   forge::api::websocket::connection>);
   (void)builder;
   return capabilities.supports(
             forge::api::core::capability::bidirectional_stream)
             ? 0
             : 1;
}
