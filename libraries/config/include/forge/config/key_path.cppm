module;

#include <string>
#include <vector>

export module forge.config.key_path;

export namespace forge::config {

struct key_path {
   std::string value;

   [[nodiscard]] std::vector<std::string> segments() const;
};

} // namespace forge::config
