module;

export module fcl.quic.transport;

import fcl.transport.endpoint;
import fcl.transport.session;
import fcl.transport.stream;
import fcl.quic.connection;
import fcl.quic.endpoint;
import fcl.quic.stream;

export namespace fcl::quic {

[[nodiscard]] fcl::transport::endpoint to_transport_endpoint(const endpoint& value);
[[nodiscard]] endpoint from_transport_endpoint(const fcl::transport::endpoint& value);

[[nodiscard]] fcl::transport::stream as_transport_stream(stream value);
[[nodiscard]] fcl::transport::session as_transport_session(connection value);

} // namespace fcl::quic
