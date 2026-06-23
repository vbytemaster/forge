module;
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

export module forge.core.chrono;

export namespace forge::chrono {
using sys_time_us = std::chrono::sys_time<std::chrono::microseconds>;
using sys_seconds = std::chrono::sys_seconds;

[[nodiscard]] sys_time_us now_us();
[[nodiscard]] constexpr sys_time_us min_time_point() noexcept {
   return sys_time_us{std::chrono::microseconds{0}};
}
[[nodiscard]] constexpr sys_time_us max_time_point() noexcept {
   return sys_time_us{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}};
}

[[nodiscard]] constexpr std::uint64_t to_fc_time_point_wire(sys_time_us value) noexcept {
   return static_cast<std::uint64_t>(value.time_since_epoch().count());
}
[[nodiscard]] constexpr sys_time_us from_fc_time_point_wire(std::uint64_t value) noexcept {
   return sys_time_us{std::chrono::microseconds{static_cast<std::int64_t>(value)}};
}

[[nodiscard]] std::uint32_t to_fc_time_point_sec_wire(sys_seconds value);
[[nodiscard]] constexpr sys_seconds from_fc_time_point_sec_wire(std::uint32_t value) noexcept {
   return sys_seconds{std::chrono::seconds{value}};
}

[[nodiscard]] constexpr std::uint64_t to_fc_microseconds_wire(std::chrono::microseconds value) noexcept {
   return static_cast<std::uint64_t>(value.count());
}
[[nodiscard]] constexpr std::chrono::microseconds from_fc_microseconds_wire(std::uint64_t value) noexcept {
   return std::chrono::microseconds{static_cast<std::int64_t>(value)};
}

[[nodiscard]] std::string to_iso_string(sys_time_us value);
[[nodiscard]] std::string to_iso_string(sys_seconds value);
[[nodiscard]] std::string to_non_delimited_iso_string(sys_seconds value);
[[nodiscard]] sys_time_us from_iso_time_point(std::string_view value);
[[nodiscard]] sys_seconds from_iso_seconds(std::string_view value);

[[nodiscard]] std::string get_approximate_relative_time_string(sys_seconds event_time, sys_seconds relative_to_time,
                                                               const std::string& ago = " ago");
[[nodiscard]] std::string get_approximate_relative_time_string(sys_time_us event_time, sys_time_us relative_to_time,
                                                               const std::string& ago = " ago");
} // namespace forge::chrono
