module;

#include <cstddef>
#include <cstdint>

export module fcl.transport.limits;

export namespace fcl::transport {

struct limits {
   std::size_t max_connections = 1024;
   std::size_t max_streams_per_connection = 256;
   std::size_t max_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_bytes = 16 * 1024 * 1024;
   std::size_t max_inbound_queued_packets = 4096;
   std::uint64_t max_frame_size = 16 * 1024 * 1024;
};

} // namespace fcl::transport
