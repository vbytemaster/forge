module;

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

module forge.config.key_path;

namespace forge::config {

std::vector<std::string> key_path::segments() const {
   auto result = std::vector<std::string>{};
   auto begin = std::size_t{0};
   while (begin <= value.size()) {
      const auto end = value.find('.', begin);
      auto segment = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
      if (!segment.empty()) {
         result.push_back(std::move(segment));
      }
      if (end == std::string::npos) {
         break;
      }
      begin = end + 1;
   }
   return result;
}

} // namespace forge::config
