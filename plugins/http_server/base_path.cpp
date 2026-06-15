module;

#include <fcl/exceptions/macros.hpp>

#include <string>
#include <string_view>

module fcl.plugins.http_server.plugin;

import fcl.exceptions;
import fcl.plugins.http_server.exceptions;

#include "private/base_path.hxx"

namespace fcl::plugins::http_server::detail {

std::string normalize_base_path(std::string_view value, std::string_view field) {
   if (value.empty() || value == "/") {
      return {};
   }
   if (value.front() != '/') {
      FCL_THROW_EXCEPTION(exceptions::invalid_config, "HTTP server base path must start with /",
                          fcl::exceptions::ctx("field", std::string{field}));
   }
   while (value.size() > 1U && value.back() == '/') {
      value.remove_suffix(1U);
   }
   return std::string{value};
}

} // namespace fcl::plugins::http_server::detail
