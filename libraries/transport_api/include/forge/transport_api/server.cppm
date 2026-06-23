module;

#include <boost/asio/awaitable.hpp>

export module forge.transport.api.server;

export import forge.api.dispatcher;
export import forge.transport.api.exceptions;
export import forge.transport.api.options;
export import forge.transport.session;
export import forge.transport.stream;

export namespace forge::transport::api {

boost::asio::awaitable<void> serve_stream(forge::transport::stream stream, forge::api::binding_plan plan,
                                          options value = {});
boost::asio::awaitable<void> serve_stream(forge::transport::stream stream, forge::api::binding_plan plan, options value,
                                          forge::api::metadata trusted_metadata);
boost::asio::awaitable<void> serve_session(forge::transport::session session, forge::api::binding_plan plan,
                                           session_options value = {});

} // namespace forge::transport::api
