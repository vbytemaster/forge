module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"
#include "details/native_store_impl.hxx"

namespace forge::plugins::db::rocksdb::detail {

std::vector<std::string> normalize_families(const std::vector<std::string>& requested) {
   std::vector<std::string> names;
   names.reserve(requested.size() + 1);
   names.emplace_back(default_family_name);
   for (const auto& name : requested) {
      if (name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "RocksDB column family name must not be empty");
      }
      if (std::find(names.begin(), names.end(), name) == names.end()) {
         names.push_back(name);
      }
   }
   return names;
}

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                          forge::config::format_decode_diagnostics("invalid RocksDB config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (value.path.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "rocksdb path must not be empty");
   }
   static_cast<void>(normalize_families(value.column_families));
}

} // namespace forge::plugins::db::rocksdb::detail
