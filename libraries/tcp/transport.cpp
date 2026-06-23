module;

#include <utility>

#include <boost/asio/awaitable.hpp>

module forge.tcp.transport;

namespace forge::tcp {

void register_stream(transport::registry& registry, boost::asio::any_io_executor executor, options tcp_options) {
   registry.register_stream(
       transport::endpoint::protocol_kind::tcp,
       [executor, tcp_options] {
          return connector{executor, tcp_options}.as_transport();
       },
       [executor, tcp_options](transport::endpoint local,
                               transport::listen_options listen_options) -> boost::asio::awaitable<transport::stream_listener> {
          auto value = listener{executor, std::move(local), listen_options, tcp_options};
          co_return value.as_transport();
       });
}

} // namespace forge::tcp
