import fcl.quic.transport;

int main() {
   const auto limits = fcl::quic::from_transport_limits({});
   return limits.max_connections == 0 ? 1 : 0;
}
