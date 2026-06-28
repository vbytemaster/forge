module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

export module forge.http.negotiation;

export namespace forge::http {

struct media_type_match {
   std::string_view type;
   std::string_view structured_suffix;
};

[[nodiscard]] std::string normalize_token(std::string_view value) {
   auto begin = std::size_t{0};
   while (begin != value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
      ++begin;
   }
   auto end = value.size();
   while (end != begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
      --end;
   }

   auto output = std::string{};
   output.reserve(end - begin);
   for (auto index = begin; index != end; ++index) {
      output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value[index]))));
   }
   return output;
}

[[nodiscard]] std::string normalize_media_type(std::string_view value) {
   const auto semicolon = value.find(';');
   return normalize_token(value.substr(0, semicolon));
}

} // namespace forge::http

namespace forge::http::detail {

[[nodiscard]] inline bool has_structured_suffix(std::string_view type, std::string_view suffix) {
   return !suffix.empty() && type.size() > suffix.size() && type.ends_with(suffix);
}

[[nodiscard]] inline std::string_view media_type_family(std::string_view type) {
   const auto slash = type.find('/');
   if (slash == std::string_view::npos) {
      return {};
   }
   return type.substr(0, slash);
}

[[nodiscard]] inline std::string_view media_type_subtype(std::string_view type) {
   const auto slash = type.find('/');
   if (slash == std::string_view::npos || slash + 1U >= type.size()) {
      return {};
   }
   return type.substr(slash + 1U);
}

[[nodiscard]] inline std::uint16_t parse_quality(std::string_view raw) {
   auto value = normalize_token(raw);
   if (value.empty()) {
      return 1000U;
   }
   if (value == "1") {
      return 1000U;
   }
   if (value == "0") {
      return 0U;
   }
   if (value.starts_with("1.")) {
      return std::all_of(value.begin() + 2, value.end(), [](char character) {
                return character == '0';
             })
                ? 1000U
                : 0U;
   }
   if (!value.starts_with("0.")) {
      return 1000U;
   }

   auto result = std::uint16_t{0};
   auto multiplier = std::uint16_t{100};
   for (auto index = std::size_t{2}; index != value.size() && multiplier != 0U; ++index) {
      const auto character = value[index];
      if (character < '0' || character > '9') {
         return 1000U;
      }
      result = static_cast<std::uint16_t>(result + static_cast<std::uint16_t>(character - '0') * multiplier);
      multiplier = static_cast<std::uint16_t>(multiplier / 10U);
   }
   return result;
}

[[nodiscard]] inline std::uint16_t item_quality(std::string_view item) {
   auto offset = std::size_t{0};
   auto first = true;
   while (offset <= item.size()) {
      const auto separator = item.find(';', offset);
      const auto end = separator == std::string_view::npos ? item.size() : separator;
      if (!first) {
         const auto parameter = item.substr(offset, end - offset);
         const auto equals = parameter.find('=');
         if (equals != std::string_view::npos &&
             normalize_token(parameter.substr(0, equals)) == "q") {
            return parse_quality(parameter.substr(equals + 1U));
         }
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
      first = false;
   }
   return 1000U;
}

[[nodiscard]] inline std::string normalize_accept_media_range(std::string_view item) {
   auto output = normalize_media_type(item);
   auto offset = std::size_t{0};
   auto first = true;
   while (offset <= item.size()) {
      const auto separator = item.find(';', offset);
      const auto end = separator == std::string_view::npos ? item.size() : separator;
      if (!first) {
         const auto parameter = normalize_token(item.substr(offset, end - offset));
         const auto equals = parameter.find('=');
         const auto name = equals == std::string::npos ? parameter : parameter.substr(0, equals);
         if (name == "q") {
            break;
         }
         if (!parameter.empty()) {
            output += ";";
            output += parameter;
         }
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
      first = false;
   }
   return output;
}

[[nodiscard]] inline std::optional<std::uint8_t> match_specificity(std::string_view range,
                                                                   const media_type_match& candidate) {
   if (range == "*/*" || range == "*") {
      return 1U;
   }

   const auto range_family = media_type_family(range);
   const auto range_subtype = media_type_subtype(range);
   if (range_family.empty() || range_subtype.empty()) {
      return std::nullopt;
   }

   if (!candidate.type.empty()) {
      const auto candidate_type = normalize_media_type(candidate.type);
      if (range == candidate_type) {
         return 4U;
      }

      const auto candidate_family = media_type_family(candidate_type);
      if (range_subtype == "*" && range_family == candidate_family) {
         return 2U;
      }
   }

   if (!candidate.structured_suffix.empty()) {
      const auto suffix = normalize_token(candidate.structured_suffix);
      if (has_structured_suffix(range, suffix)) {
         return 3U;
      }
      if (range_subtype == "*" && !candidate.type.empty() &&
          range_family == media_type_family(normalize_media_type(candidate.type))) {
         return 2U;
      }
      if (range_subtype.starts_with("*+") &&
          range_subtype.substr(1U) == suffix) {
         return 3U;
      }
   }

   return std::nullopt;
}

[[nodiscard]] inline std::optional<std::uint8_t> best_specificity(std::string_view range,
                                                                  std::span<const media_type_match> matches) {
   auto best = std::optional<std::uint8_t>{};
   for (const auto& candidate : matches) {
      if (const auto specificity = match_specificity(range, candidate);
          specificity.has_value() && (!best.has_value() || *specificity > *best)) {
         best = specificity;
      }
   }
   return best;
}

} // namespace forge::http::detail

export namespace forge::http {

[[nodiscard]] bool media_type_matches(std::string_view value, std::span<const media_type_match> matches) {
   const auto media_type = normalize_media_type(value);
   if (media_type.find('*') != std::string::npos) {
      return false;
   }
   return detail::best_specificity(media_type, matches).has_value();
}

[[nodiscard]] bool accept_allows(std::string_view value, std::span<const media_type_match> matches) {
   if (normalize_token(value).empty()) {
      return true;
   }

   auto best_specificity = std::optional<std::uint8_t>{};
   auto best_quality = std::uint16_t{0};
   auto offset = std::size_t{0};
   while (offset <= value.size()) {
      const auto separator = value.find(',', offset);
      const auto end = separator == std::string_view::npos ? value.size() : separator;
      const auto item = value.substr(offset, end - offset);
      const auto range = detail::normalize_accept_media_range(item);
      if (const auto specificity = detail::best_specificity(range, matches); specificity.has_value()) {
         const auto quality = detail::item_quality(item);
         if (!best_specificity.has_value() || *specificity > *best_specificity) {
            best_specificity = specificity;
            best_quality = quality;
         } else if (*specificity == *best_specificity) {
            best_quality = std::max(best_quality, quality);
         }
      }
      if (separator == std::string_view::npos) {
         break;
      }
      offset = separator + 1U;
   }

   return best_specificity.has_value() && best_quality != 0U;
}

} // namespace forge::http
