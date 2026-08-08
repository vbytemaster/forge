module;

#include <boost/asio/awaitable.hpp>

export module forge.api.stream.server;

export import forge.api.core.dispatcher;
export import forge.api.stream.options;
export import forge.api.stream.session;
export import forge.net.transport.stream;

export namespace forge::api::stream {

boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan,
                                          options value = {});
boost::asio::awaitable<void> serve_stream(forge::net::transport::stream stream, forge::api::core::binding_plan plan, options value,
                                          forge::api::core::metadata trusted_metadata);

} // namespace forge::api::stream
