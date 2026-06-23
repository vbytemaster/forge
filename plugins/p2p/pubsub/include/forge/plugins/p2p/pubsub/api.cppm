module;

#include <boost/asio/awaitable.hpp>
#include <forge/api/macros.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.plugins.p2p.pubsub.api;

import forge.api.exceptions;
import forge.api.types;
import forge.api.descriptor;
import forge.api.error_projection;
import forge.api.handle;
import forge.api.connection;
import forge.api.registry;
import forge.api.binding;
import forge.api.dispatcher;
import forge.p2p.pubsub;
import forge.plugins.p2p.pubsub.types;
import forge.raw.raw;

export namespace forge::plugins::p2p::pubsub {

using handler = std::function<boost::asio::awaitable<forge::p2p::pubsub::validation_result>(message)>;
template <typename T>
using typed_handler = std::function<boost::asio::awaitable<forge::p2p::pubsub::validation_result>(typed_message<T>)>;

class api : public forge::api::contract<api> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<message> publish(forge::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                                                   publish_options options = {}) = 0;
   virtual boost::asio::awaitable<subscription> subscribe(forge::p2p::pubsub::topic subject, handler callback,
                                                          subscribe_options options = {}) = 0;
   virtual boost::asio::awaitable<void> unsubscribe(subscription value) = 0;
   [[nodiscard]] virtual std::vector<subscription> subscriptions() const = 0;
   [[nodiscard]] virtual snapshot snapshot() const = 0;

   template <typename T>
      requires(!std::same_as<std::remove_cvref_t<T>, std::vector<std::uint8_t>> &&
               !std::same_as<std::remove_cvref_t<T>, std::vector<char>>)
   boost::asio::awaitable<message> publish(forge::p2p::pubsub::topic subject, const T& value,
                                           publish_options options = {}) {
      auto packed = forge::raw::pack(value);
      auto bytes = std::vector<std::uint8_t>{packed.begin(), packed.end()};
      co_return co_await publish(std::move(subject), std::move(bytes), options);
   }

   template <typename T>
   boost::asio::awaitable<subscription> subscribe(forge::p2p::pubsub::topic subject, typed_handler<T> callback,
                                                  subscribe_options options = {}) {
      auto wrapper = [callback = std::move(callback)](message value) mutable
         -> boost::asio::awaitable<forge::p2p::pubsub::validation_result> {
         auto typed = typed_message<T>{
            .source = std::move(value.source),
            .author = std::move(value.author),
            .subject = std::move(value.subject),
            .value = forge::raw::unpack<T>(value.data),
            .seqno = std::move(value.seqno),
         };
         co_return co_await callback(std::move(typed));
      };
      co_return co_await subscribe(std::move(subject), std::move(wrapper), options);
   }
};

} // namespace forge::plugins::p2p::pubsub

export {
FORGE_API(::forge::plugins::p2p::pubsub::api, FORGE_API_CONTRACT("forge.plugins.p2p.pubsub", 1, 0))
}
