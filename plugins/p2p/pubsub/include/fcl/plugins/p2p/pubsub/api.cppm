module;

#include <boost/asio/awaitable.hpp>
#include <fcl/api/macros.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

export module fcl.plugins.p2p.pubsub.api;

import fcl.api.exceptions;
import fcl.api.types;
import fcl.api.descriptor;
import fcl.api.error_projection;
import fcl.api.handle;
import fcl.api.connection;
import fcl.api.registry;
import fcl.api.binding;
import fcl.api.dispatcher;
import fcl.p2p.pubsub;
import fcl.plugins.p2p.pubsub.types;
import fcl.raw.raw;

export namespace fcl::plugins::p2p::pubsub {

using handler = std::function<boost::asio::awaitable<fcl::p2p::pubsub::validation_result>(message)>;
template <typename T>
using typed_handler = std::function<boost::asio::awaitable<fcl::p2p::pubsub::validation_result>(typed_message<T>)>;

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   virtual boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, std::vector<std::uint8_t> data,
                                                   publish_options options = {}) = 0;
   virtual boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, handler callback,
                                                          subscribe_options options = {}) = 0;
   virtual boost::asio::awaitable<void> unsubscribe(subscription value) = 0;
   [[nodiscard]] virtual std::vector<subscription> subscriptions() const = 0;
   [[nodiscard]] virtual snapshot snapshot() const = 0;

   template <typename T>
      requires(!std::same_as<std::remove_cvref_t<T>, std::vector<std::uint8_t>> &&
               !std::same_as<std::remove_cvref_t<T>, std::vector<char>>)
   boost::asio::awaitable<message> publish(fcl::p2p::pubsub::topic subject, const T& value,
                                           publish_options options = {}) {
      auto packed = fcl::raw::pack(value);
      auto bytes = std::vector<std::uint8_t>{packed.begin(), packed.end()};
      co_return co_await publish(std::move(subject), std::move(bytes), options);
   }

   template <typename T>
   boost::asio::awaitable<subscription> subscribe(fcl::p2p::pubsub::topic subject, typed_handler<T> callback,
                                                  subscribe_options options = {}) {
      auto wrapper = [callback = std::move(callback)](message value) mutable
         -> boost::asio::awaitable<fcl::p2p::pubsub::validation_result> {
         auto typed = typed_message<T>{
            .source = std::move(value.source),
            .author = std::move(value.author),
            .subject = std::move(value.subject),
            .value = fcl::raw::unpack<T>(value.data),
            .seqno = std::move(value.seqno),
         };
         co_return co_await callback(std::move(typed));
      };
      co_return co_await subscribe(std::move(subject), std::move(wrapper), options);
   }
};

} // namespace fcl::plugins::p2p::pubsub

export {
FCL_API(::fcl::plugins::p2p::pubsub::api, FCL_API_CONTRACT("fcl.plugins.p2p.pubsub", 1, 0))
}
