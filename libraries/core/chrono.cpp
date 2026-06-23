module;
#include <boost/date_time/posix_time/posix_time.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

module forge.core.chrono;

import forge.core.string;

namespace forge::chrono {
namespace {
boost::posix_time::ptime epoch() {
   return boost::posix_time::from_time_t(0);
}

boost::posix_time::ptime parse_iso_time(std::string_view value) {
   const std::string text{value};
   if (text.size() >= 5 && text.at(4) == '-') {
      return boost::date_time::parse_delimited_time<boost::posix_time::ptime>(text, 'T');
   }
   return boost::posix_time::from_iso_string(text);
}
} // namespace

sys_time_us now_us() {
   return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
}

std::uint32_t to_fc_time_point_sec_wire(sys_seconds value) {
   const auto count = value.time_since_epoch().count();
   if (count < 0 || count > std::numeric_limits<std::uint32_t>::max()) {
      throw std::out_of_range("sys_seconds is outside FC time_point_sec wire range");
   }
   return static_cast<std::uint32_t>(count);
}

std::string to_non_delimited_iso_string(sys_seconds value) {
   const auto ptime = boost::posix_time::from_time_t(static_cast<std::time_t>(value.time_since_epoch().count()));
   return boost::posix_time::to_iso_string(ptime);
}

std::string to_iso_string(sys_seconds value) {
   const auto ptime = boost::posix_time::from_time_t(static_cast<std::time_t>(value.time_since_epoch().count()));
   return boost::posix_time::to_iso_extended_string(ptime);
}

sys_seconds from_iso_seconds(std::string_view value) {
   try {
      const auto pt = parse_iso_time(value);
      const auto seconds = (pt - epoch()).total_seconds();
      if (seconds < 0 || seconds > std::numeric_limits<std::uint32_t>::max()) {
         throw std::out_of_range("ISO time is outside FC time_point_sec wire range");
      }
      return sys_seconds{std::chrono::seconds{seconds}};
   } catch (const std::exception&) {
      throw std::invalid_argument("unable to convert ISO-formatted string to std::chrono::sys_seconds: " +
                                  std::string{value});
   }
}

std::string to_iso_string(sys_time_us value) {
   const auto count = value.time_since_epoch().count();
   if (count >= 0) {
      const auto secs = static_cast<std::uint64_t>(count) / 1'000'000ULL;
      const auto msec = (static_cast<std::uint64_t>(count) % 1'000'000ULL) / 1000ULL;
      const std::string padded_ms = std::to_string(msec + 1000ULL).substr(1);
      const auto ptime = boost::posix_time::from_time_t(static_cast<std::time_t>(secs));
      return boost::posix_time::to_iso_extended_string(ptime) + "." + padded_ms;
   }

   const auto as_duration = boost::posix_time::microseconds(count);
   return boost::posix_time::to_iso_string(as_duration);
}

sys_time_us from_iso_time_point(std::string_view value) {
   try {
      const std::string text{value};
      const auto dot = text.find('.');
      if (dot == std::string::npos) {
         return sys_time_us{
             std::chrono::duration_cast<std::chrono::microseconds>(from_iso_seconds(text).time_since_epoch())};
      }

      auto millis = text.substr(dot);
      millis[0] = '1';
      while (millis.size() < 4) {
         millis.push_back('0');
      }
      const auto base =
          sys_time_us{std::chrono::duration_cast<std::chrono::microseconds>(from_iso_seconds(text).time_since_epoch())};
      return base + std::chrono::milliseconds{to_int64(millis) - 1000};
   } catch (const std::exception&) {
      throw std::invalid_argument("unable to convert ISO-formatted string to std::chrono::sys_time<microseconds>: " +
                                  std::string{value});
   }
}

std::string get_approximate_relative_time_string(sys_seconds event_time, sys_seconds relative_to_time,
                                                 const std::string& default_ago) {
   std::string ago = default_ago;
   auto seconds_ago = static_cast<std::int64_t>((relative_to_time - event_time).count());
   if (seconds_ago < 0) {
      ago = " in the future";
      seconds_ago = -seconds_ago;
   }

   std::stringstream result;
   if (seconds_ago < 90) {
      result << seconds_ago << " second" << (seconds_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto minutes_ago = static_cast<std::uint32_t>((seconds_ago + 30) / 60);
   if (minutes_ago < 90) {
      result << minutes_ago << " minute" << (minutes_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto hours_ago = (minutes_ago + 30) / 60;
   if (hours_ago < 90) {
      result << hours_ago << " hour" << (hours_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto days_ago = (hours_ago + 12) / 24;
   if (days_ago < 90) {
      result << days_ago << " day" << (days_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto weeks_ago = (days_ago + 3) / 7;
   if (weeks_ago < 70) {
      result << weeks_ago << " week" << (weeks_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto months_ago = (days_ago + 15) / 30;
   if (months_ago < 12) {
      result << months_ago << " month" << (months_ago > 1 ? "s" : "") << ago;
      return result.str();
   }

   const auto years_ago = days_ago / 365;
   result << years_ago << " year" << (months_ago > 1 ? "s" : "");
   if (months_ago < 12 * 5) {
      const auto leftover_days = days_ago - (years_ago * 365);
      const auto leftover_months = (leftover_days + 15) / 30;
      if (leftover_months) {
         result << leftover_months << " month" << (months_ago > 1 ? "s" : "");
      }
   }
   result << ago;
   return result.str();
}

std::string get_approximate_relative_time_string(sys_time_us event_time, sys_time_us relative_to_time,
                                                 const std::string& ago) {
   return get_approximate_relative_time_string(std::chrono::time_point_cast<std::chrono::seconds>(event_time),
                                               std::chrono::time_point_cast<std::chrono::seconds>(relative_to_time),
                                               ago);
}
} // namespace forge::chrono
