module;

#include <boost/asio/awaitable.hpp>

export module fcl.transport.api.server;

export import fcl.api.dispatcher;
export import fcl.transport.api.exceptions;
export import fcl.transport.api.options;
export import fcl.transport.session;
export import fcl.transport.stream;

export namespace fcl::transport::api {

boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, fcl::api::binding_plan plan,
                                          options value = {});
boost::asio::awaitable<void> serve_stream(fcl::transport::stream stream, fcl::api::binding_plan plan, options value,
                                          fcl::api::metadata trusted_metadata);
boost::asio::awaitable<void> serve_session(fcl::transport::session session, fcl::api::binding_plan plan,
                                           session_options value = {});

} // namespace fcl::transport::api
