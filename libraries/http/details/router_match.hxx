#pragma once

namespace forge::http::detail {

inline bool parameter_segment(const std::string& segment) {
   return segment.size() > 1U && segment.front() == ':';
}

inline bool parameterized(const std::vector<std::string>& segments) {
   for (const auto& segment : segments) {
      if (parameter_segment(segment)) {
         return true;
      }
   }
   return false;
}

template <typename Entry>
bool match_path(const Entry& entry, const target& parsed_target, std::unordered_map<std::string, std::string>* params) {
   if (entry.segments.size() != parsed_target.segments.size()) {
      return false;
   }

   auto captured = std::unordered_map<std::string, std::string>{};
   for (auto index = std::size_t{0}; index != entry.segments.size(); ++index) {
      const auto& pattern = entry.segments[index];
      const auto& value = parsed_target.segments[index];
      if (parameter_segment(pattern)) {
         captured.emplace(pattern.substr(1), value);
         continue;
      }
      if (pattern != value) {
         return false;
      }
   }

   if (params != nullptr) {
      *params = std::move(captured);
   }
   return true;
}

template <typename Entry> bool path_exists(const std::vector<Entry>& entries, const target& parsed_target) {
   for (const auto& entry : entries) {
      if (match_path(entry, parsed_target, nullptr)) {
         return true;
      }
   }
   return false;
}

template <typename Entry>
const Entry* find_path_match(const std::vector<Entry>& entries, const target& parsed_target,
                             std::unordered_map<std::string, std::string>& params) {
   for (const auto prefer_parameterized : {false, true}) {
      for (const auto& entry : entries) {
         if (entry.parameterized != prefer_parameterized) {
            continue;
         }
         if (match_path(entry, parsed_target, &params)) {
            return &entry;
         }
      }
   }
   return nullptr;
}

} // namespace forge::http::detail
