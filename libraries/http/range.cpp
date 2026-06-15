module;

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

module fcl.http.range;

namespace fcl::http {
namespace {

bool parse_u64(std::string_view value, std::uint64_t& output) {
   if (value.empty()) {
      return false;
   }
   const auto* first = value.data();
   const auto* last = value.data() + value.size();
   auto parsed = std::uint64_t{};
   const auto result = std::from_chars(first, last, parsed);
   if (result.ec != std::errc{} || result.ptr != last) {
      return false;
   }
   output = parsed;
   return true;
}

std::string content_range_for(byte_range range, std::uint64_t size) {
   return "bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) + "/" + std::to_string(size);
}

} // namespace

range_request parse_range_header(std::string_view value, std::uint64_t size) {
   constexpr auto prefix = std::string_view{"bytes="};
   if (!value.starts_with(prefix)) {
      return {.present = true, .satisfiable = false};
   }
   value.remove_prefix(prefix.size());
   if (value.find(',') != std::string_view::npos) {
      return {.present = true, .satisfiable = false};
   }

   const auto separator = value.find('-');
   if (separator == std::string_view::npos) {
      return {.present = true, .satisfiable = false};
   }

   auto first_text = value.substr(0, separator);
   auto last_text = value.substr(separator + 1U);
   if (size == 0) {
      return {.present = true, .satisfiable = false};
   }

   if (first_text.empty()) {
      auto suffix = std::uint64_t{};
      if (!parse_u64(last_text, suffix) || suffix == 0) {
         return {.present = true, .satisfiable = false};
      }
      const auto length = std::min<std::uint64_t>(suffix, size);
      return {
          .present = true,
          .satisfiable = true,
          .bytes = {.first = size - length, .last = size - 1U},
      };
   }

   auto first = std::uint64_t{};
   if (!parse_u64(first_text, first) || first >= size) {
      return {.present = true, .satisfiable = false};
   }

   auto last = size - 1U;
   if (!last_text.empty() && (!parse_u64(last_text, last) || last < first)) {
      return {.present = true, .satisfiable = false};
   }
   last = std::min<std::uint64_t>(last, size - 1U);

   return {
       .present = true,
       .satisfiable = true,
       .bytes = {.first = first, .last = last},
   };
}

range_response resolve_range(std::optional<std::string_view> header, std::uint64_t size) {
   if (!header.has_value()) {
      return {
          .partial = false,
          .satisfiable = true,
          .bytes = {.first = 0, .last = size == 0 ? 0 : size - 1U},
          .total_size = size,
      };
   }

   const auto parsed = parse_range_header(*header, size);
   if (!parsed.satisfiable) {
      return {
          .partial = false,
          .satisfiable = false,
          .total_size = size,
          .content_range = "bytes */" + std::to_string(size),
      };
   }

   return {
       .partial = true,
       .satisfiable = true,
       .bytes = parsed.bytes,
       .total_size = size,
       .content_range = content_range_for(parsed.bytes, size),
   };
}

} // namespace fcl::http
