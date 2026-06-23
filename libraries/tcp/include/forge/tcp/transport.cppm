module;

#include <boost/asio/any_io_executor.hpp>

export module forge.tcp.transport;

export import forge.tcp.connector;
export import forge.tcp.listener;
export import forge.transport.registry;

export namespace forge::tcp {

void register_stream(transport::registry& registry, boost::asio::any_io_executor executor, options tcp_options = {});

} // namespace forge::tcp
