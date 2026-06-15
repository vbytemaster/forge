module;

#include <boost/asio/awaitable.hpp>

export module fcl.api.transport.server;

export import fcl.api.dispatcher;
export import fcl.api.transport.exceptions;
export import fcl.api.transport.options;
export import fcl.transport.session;
export import fcl.transport.stream;

export namespace fcl::api::transport {

boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, binding_plan plan, options value = {});
boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, binding_plan plan, options value,
                                          fcl::api::metadata trusted_metadata);
boost::asio::awaitable<void> serve_session(fcl::transport::session session, binding_plan plan,
                                           session_options value = {});

} // namespace fcl::api::transport
