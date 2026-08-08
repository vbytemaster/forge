module;

#include <chrono>
#include <cstddef>
#include <cstdint>

export module forge.api.stream.options;

export import forge.api.core.types;

export namespace forge::api::stream {

struct options {
   forge::api::core::protocol_version version{.major = 2, .minor = 0};
   forge::api::core::capability_set capabilities;
   forge::api::core::codec_id codec{.value = "forge.raw"};
   std::size_t max_inflight = 128;
   std::chrono::milliseconds deadline{0};
   std::uint32_t max_frame_size = 2U * 1024U * 1024U;
   std::uint32_t max_item_size = 1024U * 1024U;
   std::uint32_t initial_window_items = 16;
   std::uint64_t initial_window_bytes = 1024U * 1024U;
   std::uint64_t max_buffered_bytes = 16U * 1024U * 1024U;
   std::chrono::milliseconds disconnect_grace{5'000};
   std::chrono::milliseconds control_timeout{5'000};
   std::chrono::milliseconds idle_timeout{60'000};
   std::size_t max_tombstones = 256;
};

struct call_options {
   forge::api::core::call_id id{};
   forge::api::core::metadata meta;
   std::chrono::milliseconds deadline{0};
};

} // namespace forge::api::stream
