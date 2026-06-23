module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.transport.api.options;

export import forge.api.types;

export namespace forge::transport::api {

struct options {
   forge::api::codec_id codec{.value = "forge.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   std::uint32_t max_frame_size = 16 * 1024 * 1024;
};

struct call_options {
   forge::api::call_id id{};
   forge::api::metadata meta;
   std::chrono::milliseconds deadline{0};
};

struct session_options {
   options stream;
   std::size_t max_concurrent_streams = 128;
};

} // namespace forge::transport::api
