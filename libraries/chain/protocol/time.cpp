module;

#include <array>
#include <cstdint>
#include <limits>
#include <string>

module forge.chain.protocol.time;

namespace forge::chain::protocol {

namespace {

constexpr bool is_leap(std::int64_t year) {
   return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr std::int64_t days_from_civil(std::int64_t year, unsigned month, unsigned day) {
   year -= month <= 2;
   const auto era = (year >= 0 ? year : year - 399) / 400;
   const auto year_of_era = static_cast<unsigned>(year - era * 400);
   const auto day_of_year = (153U * (month > 2 ? month - 3U : month + 9U) + 2U) / 5U + day - 1U;
   const auto day_of_era = year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
   return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

struct civil_date {
   std::int64_t year;
   unsigned month;
   unsigned day;
};

constexpr civil_date civil_from_days(std::int64_t days) {
   days += 719468;
   const auto era = (days >= 0 ? days : days - 146096) / 146097;
   const auto day_of_era = static_cast<unsigned>(days - era * 146097);
   const auto year_of_era = (day_of_era - day_of_era / 1460U + day_of_era / 36524U - day_of_era / 146096U) / 365U;
   auto year = static_cast<std::int64_t>(year_of_era) + era * 400;
   const auto day_of_year = day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
   const auto month_part = (5U * day_of_year + 2U) / 153U;
   const auto day = day_of_year - (153U * month_part + 2U) / 5U + 1U;
   const auto month = month_part < 10U ? month_part + 3U : month_part - 9U;
   year += month <= 2U;
   return {year, month, day};
}

unsigned parse_digits(const std::string& text, std::size_t offset, std::size_t count) {
   auto result = 0U;
   for (auto index = std::size_t{}; index < count; ++index) {
      const auto character = text[offset + index];
      if (character < '0' || character > '9') {
         detail::fail_value("date parsing failed");
      }
      result = result * 10U + static_cast<unsigned>(character - '0');
   }
   return result;
}

std::int64_t parse_seconds(const std::string& text) {
   if (text.size() != 19U || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
       text[16] != ':') {
      detail::fail_value("date parsing failed");
   }
   const auto year = parse_digits(text, 0U, 4U);
   const auto month = parse_digits(text, 5U, 2U);
   const auto day = parse_digits(text, 8U, 2U);
   const auto hour = parse_digits(text, 11U, 2U);
   const auto minute = parse_digits(text, 14U, 2U);
   const auto second = parse_digits(text, 17U, 2U);
   constexpr auto month_days = std::array<unsigned, 12>{31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
   if (month == 0U || month > 12U || day == 0U ||
       day > month_days[month - 1U] + (month == 2U && is_leap(year) ? 1U : 0U) || hour > 23U || minute > 59U ||
       second > 59U) {
      detail::fail_value("date parsing failed");
   }
   return days_from_civil(year, month, day) * 86'400 + static_cast<std::int64_t>(hour * 3'600U + minute * 60U + second);
}

std::uint32_t parse_fractional_microseconds(const std::string& text) {
   if (text.size() < 21U || text.size() > 26U || text[19] != '.') {
      detail::fail_value("date parsing failed");
   }

   const auto digits = text.size() - 20U;
   auto fraction = parse_digits(text, 20U, digits);
   for (auto index = digits; index < 6U; ++index) {
      fraction *= 10U;
   }
   return fraction;
}

void append_decimal(std::string& result, std::uint64_t value, std::size_t width) {
   const auto offset = result.size();
   result.append(width, '0');
   for (auto index = std::size_t{}; index < width; ++index) {
      result[offset + width - 1U - index] = static_cast<char>('0' + value % 10U);
      value /= 10U;
   }
}

std::string format_seconds(std::int64_t seconds) {
   auto days = seconds / 86'400;
   auto remainder = seconds % 86'400;
   if (remainder < 0) {
      remainder += 86'400;
      --days;
   }
   const auto date = civil_from_days(days);
   auto result = std::string{};
   result.reserve(19U);
   append_decimal(result, static_cast<std::uint64_t>(date.year), 4U);
   result.push_back('-');
   append_decimal(result, date.month, 2U);
   result.push_back('-');
   append_decimal(result, date.day, 2U);
   result.push_back('T');
   append_decimal(result, static_cast<std::uint64_t>(remainder / 3'600), 2U);
   result.push_back(':');
   append_decimal(result, static_cast<std::uint64_t>((remainder / 60) % 60), 2U);
   result.push_back(':');
   append_decimal(result, static_cast<std::uint64_t>(remainder % 60), 2U);
   return result;
}

} // namespace

time_point time_point::from_iso_string(const std::string& text) {
   if (text.size() == 19U) {
      return time_point{protocol::seconds(parse_seconds(text))};
   }
   const auto seconds = parse_seconds(text.substr(0U, 19U));
   return time_point{protocol::seconds(seconds) + microseconds{parse_fractional_microseconds(text)}};
}

std::string time_point::to_string() const {
   auto seconds = time_since_epoch().count() / 1'000'000;
   auto fraction = time_since_epoch().count() % 1'000'000;
   if (fraction < 0) {
      fraction += 1'000'000;
      --seconds;
   }

   auto result = format_seconds(seconds);
   if (fraction == 0) {
      return result;
   }
   result.push_back('.');
   if (fraction % 1'000 == 0) {
      append_decimal(result, static_cast<std::uint64_t>(fraction / 1'000), 3U);
   } else {
      append_decimal(result, static_cast<std::uint64_t>(fraction), 6U);
   }
   return result;
}

time_point_sec time_point_sec::from_iso_string(const std::string& text) {
   const auto value = parse_seconds(text);
   if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) {
      detail::fail_value("date parsing failed");
   }
   return time_point_sec{static_cast<std::uint32_t>(value)};
}

std::string time_point_sec::to_string() const {
   return format_seconds(utc_seconds);
}

block_timestamp::block_timestamp(time_point value) noexcept {
   *this = value;
}

block_timestamp::block_timestamp(time_point_sec value) noexcept : block_timestamp{static_cast<time_point>(value)} {}

block_timestamp block_timestamp::next() const {
   if (slot == std::numeric_limits<std::uint32_t>::max()) {
      detail::fail_value("block timestamp overflow");
   }
   return block_timestamp{slot + 1U};
}

time_point block_timestamp::to_time_point() const noexcept {
   return static_cast<time_point>(*this);
}

block_timestamp block_timestamp::from_iso_string(const std::string& text) {
   const auto has_fraction = text.size() == 23U && text[19] == '.';
   if (text.size() != 19U && !has_fraction) {
      detail::fail_value("date parsing failed");
   }

   const auto seconds = parse_seconds(text.substr(0U, 19U));
   const auto fraction = has_fraction ? parse_digits(text, 20U, 3U) : 0U;
   if (fraction != 0U && fraction != static_cast<unsigned>(block_interval_ms)) {
      detail::fail_value("date parsing failed");
   }

   const auto milliseconds_since_epoch = seconds * 1'000 + static_cast<std::int64_t>(fraction);
   const auto elapsed = milliseconds_since_epoch - block_timestamp_epoch;
   if (elapsed < 0 || elapsed % block_interval_ms != 0 ||
       elapsed / block_interval_ms > std::numeric_limits<std::uint32_t>::max()) {
      detail::fail_value("date parsing failed");
   }
   return block_timestamp{static_cast<std::uint32_t>(elapsed / block_interval_ms)};
}

std::string block_timestamp::to_string() const {
   auto result = format_seconds(block_timestamp_epoch / 1'000 + static_cast<std::int64_t>(slot / 2U));
   if (slot % 2U != 0U) {
      result += ".500";
   }
   return result;
}

block_timestamp& block_timestamp::operator=(time_point value) noexcept {
   const auto milliseconds_since_epoch = value.time_since_epoch().count() / 1'000;
   slot = static_cast<std::uint32_t>((milliseconds_since_epoch - block_timestamp_epoch) / block_interval_ms);
   return *this;
}

#if !defined(FORGE_CONTRACT_GUEST)
void to_variant(const time_point& value, forge::variant& output) {
   output = value.to_string();
}

void from_variant(const forge::variant& value, time_point& output) {
   output = time_point::from_iso_string(value.as_string());
}

void to_variant(const time_point_sec& value, forge::variant& output) {
   output = value.to_string();
}

void from_variant(const forge::variant& value, time_point_sec& output) {
   output = time_point_sec::from_iso_string(value.as_string());
}

void to_variant(const block_timestamp& value, forge::variant& output) {
   output = value.to_string();
}

void from_variant(const forge::variant& value, block_timestamp& output) {
   output = block_timestamp::from_iso_string(value.as_string());
}
#endif

} // namespace forge::chain::protocol
