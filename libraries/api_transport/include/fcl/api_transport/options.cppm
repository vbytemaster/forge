module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module fcl.api.transport.options;

export import fcl.api.types;

export namespace fcl::api::transport {

struct options {
   codec_id codec{.value = "fcl.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   std::uint32_t max_frame_size = 16 * 1024 * 1024;
};

struct call_options {
   call_id id{};
   metadata meta;
   std::chrono::milliseconds deadline{0};
};

struct session_options {
   options stream;
   std::size_t max_concurrent_streams = 128;
};

} // namespace fcl::api::transport
