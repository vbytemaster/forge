module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

export module forge.plugins.log.otlp.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.plugins.log.otlp.types;

export namespace forge::plugins::log::otlp {

class api : public forge::api::contract<api, forge::api::surface::local> {
 public:
   virtual ~api() = default;

   boost::asio::awaitable<void> flush() {
      (void)co_await flush(flush_request{});
      co_return;
   }

   boost::asio::awaitable<::forge::plugins::log::otlp::metrics> metrics() {
      co_return co_await metrics(metrics_request{});
   }

   virtual boost::asio::awaitable<flush_result> flush(flush_request value) = 0;
   virtual boost::asio::awaitable<::forge::plugins::log::otlp::metrics> metrics(metrics_request value) = 0;
};

} // namespace forge::plugins::log::otlp

export {
FORGE_API(::forge::plugins::log::otlp::api, FORGE_API_CONTRACT("forge.plugins.log.otlp", 1, 0),
        FORGE_API_METHOD_TYPED(flush, ::forge::plugins::log::otlp::flush_request, ::forge::plugins::log::otlp::flush_result),
        FORGE_API_METHOD_TYPED(metrics,
                             ::forge::plugins::log::otlp::metrics_request,
                             ::forge::plugins::log::otlp::metrics))
}
