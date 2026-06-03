module;

export module fcl.quic.transport;

import fcl.asio.runtime;
import fcl.transport.connector;
import fcl.transport.endpoint;
import fcl.transport.listener;
import fcl.transport.registry;
import fcl.transport.session;
import fcl.transport.stream;
import fcl.quic.connection;
import fcl.quic.connector;
import fcl.quic.endpoint;
import fcl.quic.listener;
import fcl.quic.options;
import fcl.quic.stream;

export namespace fcl::quic {

[[nodiscard]] fcl::transport::limits to_transport_limits(const transport_limits& value);
[[nodiscard]] transport_limits from_transport_limits(const fcl::transport::limits& value);

[[nodiscard]] fcl::transport::endpoint to_transport_endpoint(const endpoint& value);
[[nodiscard]] endpoint from_transport_endpoint(const fcl::transport::endpoint& value);

[[nodiscard]] fcl::transport::stream as_transport_stream(stream value);
[[nodiscard]] fcl::transport::session as_transport_session(connection value);

[[nodiscard]] fcl::transport::session_connector make_session_connector(fcl::asio::runtime& runtime,
                                                                       client_options options = {});
[[nodiscard]] fcl::transport::session_listener make_session_listener(fcl::asio::runtime& runtime,
                                                                     fcl::transport::endpoint local,
                                                                     server_options options = {},
                                                                     fcl::transport::listen_options listen_options = {});

void register_session(fcl::transport::registry& registry, fcl::asio::runtime& runtime,
                      client_options client = {}, server_options server = {});

} // namespace fcl::quic
