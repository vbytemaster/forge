module;

export module forge.quic.transport;

import forge.asio.runtime;
import forge.transport.connector;
import forge.transport.endpoint;
import forge.transport.listener;
import forge.transport.registry;
import forge.transport.session;
import forge.transport.stream;
import forge.quic.connection;
import forge.quic.connector;
import forge.quic.endpoint;
import forge.quic.listener;
import forge.quic.options;
import forge.quic.stream;

export namespace forge::quic {

[[nodiscard]] forge::transport::limits to_transport_limits(const transport_limits& value);
[[nodiscard]] transport_limits from_transport_limits(const forge::transport::limits& value);

[[nodiscard]] forge::transport::endpoint to_transport_endpoint(const endpoint& value);
[[nodiscard]] endpoint from_transport_endpoint(const forge::transport::endpoint& value);

[[nodiscard]] forge::transport::stream as_transport_stream(stream value);
[[nodiscard]] forge::transport::session as_transport_session(connection value);

[[nodiscard]] forge::transport::session_connector make_session_connector(forge::asio::runtime& runtime,
                                                                       client_options options = {});
[[nodiscard]] forge::transport::session_listener make_session_listener(forge::asio::runtime& runtime,
                                                                     forge::transport::endpoint local,
                                                                     server_options options = {},
                                                                     forge::transport::listen_options listen_options = {});

void register_session(forge::transport::registry& registry, forge::asio::runtime& runtime,
                      client_options client = {}, server_options server = {});

} // namespace forge::quic
