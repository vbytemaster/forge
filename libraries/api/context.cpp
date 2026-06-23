module;

#include <optional>
#include <string>
#include <string_view>

module forge.api.context;

namespace forge::api {

std::optional<std::string> metadata_value(const metadata& value, std::string_view key) {
   for (const auto& item : value) {
      if (item.key == key) {
         return item.value;
      }
   }
   return std::nullopt;
}

} // namespace forge::api
