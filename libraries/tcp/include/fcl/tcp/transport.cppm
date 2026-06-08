module;

#include <boost/asio/any_io_executor.hpp>

export module fcl.tcp.transport;

export import fcl.tcp.connector;
export import fcl.tcp.listener;
export import fcl.transport.registry;

export namespace fcl::tcp {

void register_stream(transport::registry& registry, boost::asio::any_io_executor executor, options tcp_options = {});

} // namespace fcl::tcp
