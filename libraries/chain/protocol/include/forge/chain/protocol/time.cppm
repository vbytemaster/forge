module;

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>

export module forge.chain.protocol.time;

import forge.chain.protocol.values;
import forge.raw.codec;
#if !defined(FORGE_CONTRACT_GUEST)
import forge.variant.value;
#endif

export namespace forge::chain::protocol {

class microseconds {
 public:
   constexpr explicit microseconds(std::int64_t count = 0) noexcept : _count(count) {}
   constexpr explicit microseconds(std::chrono::microseconds value) noexcept : _count(value.count()) {}

   [[nodiscard]] static constexpr microseconds maximum() noexcept {
      return microseconds{std::chrono::microseconds::max()};
   }

   [[nodiscard]] constexpr std::int64_t count() const noexcept {
      return _count;
   }

   [[nodiscard]] constexpr std::int64_t to_seconds() const noexcept {
      return _count / 1'000'000;
   }

   [[nodiscard]] constexpr std::chrono::microseconds chrono() const noexcept {
      return std::chrono::microseconds{_count};
   }

   constexpr explicit operator std::chrono::microseconds() const noexcept {
      return chrono();
   }

   constexpr microseconds& operator+=(microseconds value) noexcept {
      _count += value._count;
      return *this;
   }

   constexpr microseconds& operator-=(microseconds value) noexcept {
      _count -= value._count;
      return *this;
   }

   friend constexpr microseconds operator+(microseconds left, microseconds right) noexcept {
      return left += right;
   }

   friend constexpr microseconds operator-(microseconds left, microseconds right) noexcept {
      return left -= right;
   }

   constexpr bool operator==(const microseconds&) const = default;
   constexpr auto operator<=>(const microseconds&) const = default;

   std::int64_t _count = 0;
};

[[nodiscard]] constexpr microseconds seconds(std::int64_t value) noexcept {
   return microseconds{value * 1'000'000};
}

[[nodiscard]] constexpr microseconds milliseconds(std::int64_t value) noexcept {
   return microseconds{value * 1'000};
}

[[nodiscard]] constexpr microseconds minutes(std::int64_t value) noexcept {
   return seconds(value * 60);
}

[[nodiscard]] constexpr microseconds hours(std::int64_t value) noexcept {
   return minutes(value * 60);
}

[[nodiscard]] constexpr microseconds days(std::int64_t value) noexcept {
   return hours(value * 24);
}

class time_point {
 public:
   using chrono_type = std::chrono::sys_time<std::chrono::microseconds>;

   constexpr explicit time_point(microseconds value = microseconds{}) noexcept : elapsed(value) {}
   constexpr explicit time_point(chrono_type value) noexcept : elapsed(value.time_since_epoch()) {}

   [[nodiscard]] constexpr const microseconds& time_since_epoch() const noexcept {
      return elapsed;
   }

   [[nodiscard]] constexpr std::uint32_t sec_since_epoch() const noexcept {
      return static_cast<std::uint32_t>(elapsed.to_seconds());
   }

   [[nodiscard]] constexpr chrono_type chrono() const noexcept {
      return chrono_type{std::chrono::microseconds{elapsed.count()}};
   }

   constexpr explicit operator chrono_type() const noexcept {
      return chrono();
   }

   [[nodiscard]] static time_point from_iso_string(const std::string& text);
   [[nodiscard]] std::string to_string() const;

   constexpr time_point& operator+=(microseconds value) noexcept {
      elapsed += value;
      return *this;
   }

   constexpr time_point& operator-=(microseconds value) noexcept {
      elapsed -= value;
      return *this;
   }

   friend constexpr time_point operator+(time_point left, microseconds right) noexcept {
      return left += right;
   }

   friend constexpr time_point operator+(time_point left, time_point right) noexcept {
      return time_point{left.elapsed + right.elapsed};
   }

   friend constexpr time_point operator-(time_point left, microseconds right) noexcept {
      return left -= right;
   }

   friend constexpr microseconds operator-(time_point left, time_point right) noexcept {
      return left.elapsed - right.elapsed;
   }

   constexpr bool operator==(const time_point&) const = default;
   constexpr auto operator<=>(const time_point&) const = default;

   microseconds elapsed{};
};

class time_point_sec {
 public:
   using chrono_type = std::chrono::sys_seconds;

   constexpr time_point_sec() = default;
   constexpr explicit time_point_sec(std::uint32_t seconds) noexcept : utc_seconds(seconds) {}
   constexpr time_point_sec(time_point value) noexcept : utc_seconds(value.sec_since_epoch()) {}
   constexpr time_point_sec(chrono_type value) noexcept
       : utc_seconds(static_cast<std::uint32_t>(value.time_since_epoch().count())) {}

   [[nodiscard]] static constexpr time_point_sec maximum() noexcept {
      return time_point_sec{0xffffffffU};
   }

   [[nodiscard]] static constexpr time_point_sec min() noexcept {
      return time_point_sec{};
   }

   [[nodiscard]] static time_point_sec from_iso_string(const std::string& text);
   [[nodiscard]] std::string to_string() const;

   constexpr operator time_point() const noexcept {
      return time_point{protocol::seconds(utc_seconds)};
   }

   [[nodiscard]] constexpr std::uint32_t sec_since_epoch() const noexcept {
      return utc_seconds;
   }

   [[nodiscard]] constexpr chrono_type chrono() const noexcept {
      return chrono_type{std::chrono::seconds{utc_seconds}};
   }

   constexpr explicit operator chrono_type() const noexcept {
      return chrono();
   }

   constexpr time_point_sec& operator=(time_point value) noexcept {
      utc_seconds = value.sec_since_epoch();
      return *this;
   }

   constexpr time_point_sec& operator=(chrono_type value) noexcept {
      utc_seconds = static_cast<std::uint32_t>(value.time_since_epoch().count());
      return *this;
   }

   constexpr time_point_sec& operator+=(std::uint32_t value) noexcept {
      utc_seconds += value;
      return *this;
   }

   constexpr time_point_sec& operator+=(microseconds value) noexcept {
      utc_seconds += static_cast<std::uint32_t>(value.to_seconds());
      return *this;
   }

   constexpr time_point_sec& operator+=(time_point_sec value) noexcept {
      utc_seconds += value.utc_seconds;
      return *this;
   }

   constexpr time_point_sec& operator-=(std::uint32_t value) noexcept {
      utc_seconds -= value;
      return *this;
   }

   constexpr time_point_sec& operator-=(microseconds value) noexcept {
      utc_seconds -= static_cast<std::uint32_t>(value.to_seconds());
      return *this;
   }

   constexpr time_point_sec& operator-=(time_point_sec value) noexcept {
      utc_seconds -= value.utc_seconds;
      return *this;
   }

   friend constexpr time_point_sec operator+(time_point_sec left, std::uint32_t right) noexcept {
      return left += right;
   }

   friend constexpr time_point_sec operator-(time_point_sec left, std::uint32_t right) noexcept {
      return left -= right;
   }

   friend constexpr time_point operator+(time_point_sec left, microseconds right) noexcept {
      return static_cast<time_point>(left) + right;
   }

   friend constexpr time_point operator-(time_point_sec left, microseconds right) noexcept {
      return static_cast<time_point>(left) - right;
   }

   friend constexpr microseconds operator-(time_point_sec left, time_point_sec right) noexcept {
      return static_cast<time_point>(left) - static_cast<time_point>(right);
   }

   friend constexpr microseconds operator-(time_point left, time_point_sec right) noexcept {
      return left - static_cast<time_point>(right);
   }

   constexpr bool operator==(const time_point_sec&) const = default;
   constexpr auto operator<=>(const time_point_sec&) const = default;

   std::uint32_t utc_seconds = 0;
};

struct block_timestamp {
   static constexpr std::int32_t block_interval_ms = 500;
   static constexpr std::int64_t block_timestamp_epoch = 946'684'800'000LL;

   std::uint32_t slot = 0;

   constexpr block_timestamp() noexcept = default;
   constexpr explicit block_timestamp(std::uint32_t raw_slot) noexcept : slot(raw_slot) {}
   explicit block_timestamp(time_point value) noexcept;
   explicit block_timestamp(time_point_sec value) noexcept;

   // CDT and Spring expose 0xffff as the contract-visible maximum even though slot is 32-bit.
   [[nodiscard]] static constexpr block_timestamp maximum() noexcept {
      return block_timestamp{0xffffU};
   }

   [[nodiscard]] static constexpr block_timestamp min() noexcept {
      return block_timestamp{};
   }

   [[nodiscard]] block_timestamp next() const;
   [[nodiscard]] time_point to_time_point() const noexcept;

   constexpr operator time_point() const noexcept {
      const auto elapsed = static_cast<std::int64_t>(slot) * block_interval_ms + block_timestamp_epoch;
      return time_point{milliseconds(elapsed)};
   }

   [[nodiscard]] static block_timestamp from_iso_string(const std::string& text);
   [[nodiscard]] std::string to_string() const;

   block_timestamp& operator=(time_point value) noexcept;

   constexpr bool operator==(const block_timestamp&) const = default;
   constexpr auto operator<=>(const block_timestamp&) const = default;
};

using block_timestamp_type = block_timestamp;

#if !defined(FORGE_CONTRACT_GUEST)
void to_variant(const time_point& value, forge::variant& output);
void from_variant(const forge::variant& value, time_point& output);
void to_variant(const time_point_sec& value, forge::variant& output);
void from_variant(const forge::variant& value, time_point_sec& output);
void to_variant(const block_timestamp& value, forge::variant& output);
void from_variant(const forge::variant& value, block_timestamp& output);
#endif

template <typename Stream> void raw_pack(Stream& stream, const microseconds& value) {
   forge::raw::pack(stream, value.count());
}

template <typename Stream> void raw_unpack(Stream& stream, microseconds& value) {
   auto count = std::int64_t{};
   forge::raw::unpack(stream, count);
   value = microseconds{count};
}

template <typename Stream> void raw_pack(Stream& stream, const time_point& value) {
   forge::raw::pack(stream, value.time_since_epoch());
}

template <typename Stream> void raw_unpack(Stream& stream, time_point& value) {
   auto elapsed = microseconds{};
   forge::raw::unpack(stream, elapsed);
   value = time_point{elapsed};
}

template <typename Stream> void raw_pack(Stream& stream, const time_point_sec& value) {
   forge::raw::pack(stream, value.sec_since_epoch());
}

template <typename Stream> void raw_unpack(Stream& stream, time_point_sec& value) {
   auto seconds = std::uint32_t{};
   forge::raw::unpack(stream, seconds);
   value = time_point_sec{seconds};
}

template <typename Stream> void raw_pack(Stream& stream, const block_timestamp& value) {
   forge::raw::pack(stream, value.slot);
}

template <typename Stream> void raw_unpack(Stream& stream, block_timestamp& value) {
   forge::raw::unpack(stream, value.slot);
}

} // namespace forge::chain::protocol
