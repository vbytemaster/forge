module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

module forge.plugins.db.store.plugin;

import forge.config.core.component;
import forge.config.core.decode;
import forge.db.core.record;
import forge.db.object.store;
import forge.exceptions;
import forge.plugins.db.store.exceptions;
import forge.plugins.db.store.types;

#include "details/config.hxx"

namespace forge::plugins::db::store::detail {

config decode_config(const forge::config::core::component_view& view) {
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, forge::config::core::format_decode_diagnostics(
                                                            "invalid DB Store plugin config", decoded.diagnostics));
   }
   validate_config(decoded.value);
   return std::move(decoded.value);
}

forge::db::object::store::options parse_object_options(const object_layer_config& value,
                                                       const std::string& store_name) {
   auto options = forge::db::object::store::options{};
   if (value.write_policy == "single-writer") {
      options.writes = forge::db::object::write_policy::single_writer;
   } else if (value.write_policy == "backend") {
      options.writes = forge::db::object::write_policy::backend;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store object write-policy is unsupported",
                            forge::exceptions::ctx("store", store_name),
                            forge::exceptions::ctx("write-policy", value.write_policy));
   }
   if (value.id_allocation == "monotonic") {
      options.id_allocation = forge::db::object::id_allocation_policy::monotonic;
   } else if (value.id_allocation == "transactional") {
      options.id_allocation = forge::db::object::id_allocation_policy::transactional;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store object id-allocation is unsupported",
                            forge::exceptions::ctx("store", store_name),
                            forge::exceptions::ctx("id-allocation", value.id_allocation));
   }
   if (options.id_allocation == forge::db::object::id_allocation_policy::transactional &&
       options.writes != forge::db::object::write_policy::single_writer) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            "transactional db object id allocation requires single-writer policy",
                            forge::exceptions::ctx("store", store_name));
   }
   return options;
}

namespace {

[[nodiscard]] bool uses_rocksdb_blob_options(const blob_data_options& value) noexcept {
   return value.enable_blob_files || value.min_blob_size != 0 || value.blob_file_size != default_blob_file_size ||
          value.blob_compression_type != "none" || value.enable_blob_garbage_collection ||
          value.blob_garbage_collection_age_cutoff != 0.25;
}

void validate_mdbx_options(const store_config& value) {
   const auto options = value.mdbx.value_or(mdbx_driver_config{});
   if (options.durability != "durable-sync" && options.durability != "safe-nosync") {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX durability is unsupported",
                            forge::exceptions::ctx("store", value.name),
                            forge::exceptions::ctx("durability", options.durability));
   }
   if (options.max_readers == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX max-readers must be non-zero",
                            forge::exceptions::ctx("store", value.name));
   }
   if (options.lane.max_pending_operations == 0 || options.lane.max_waiting_submissions == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX lane bounds must be non-zero",
                            forge::exceptions::ctx("store", value.name));
   }
   if (options.lane.thread_name && options.lane.thread_name->empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX lane thread-name must not be empty",
                            forge::exceptions::ctx("store", value.name));
   }

   const auto ordered = [](const auto& lower, const auto& upper) { return !lower || !upper || *lower <= *upper; };
   if (!ordered(options.map.lower_size, options.map.current_size) ||
       !ordered(options.map.current_size, options.map.upper_size) ||
       !ordered(options.map.lower_size, options.map.upper_size)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX map must satisfy lower <= current <= upper",
                            forge::exceptions::ctx("store", value.name));
   }

   if (value.blob && uses_rocksdb_blob_options(value.blob->data_blobs)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX does not support RocksDB Blob-file options",
                            forge::exceptions::ctx("store", value.name));
   }
}

} // namespace

store_options parse_options(const store_config& value) {
   auto options = store_options{
       .object = std::nullopt,
       .blob = std::nullopt,
       .revision = std::nullopt,
   };
   if (value.object) {
      options.object = object_layer_options{
          .family = forge::db::core::family{value.object->family},
          .runtime = parse_object_options(*value.object, value.name),
      };
   }
   if (value.blob) {
      options.blob = blob_layer_options{
          .data_family = forge::db::core::family{value.blob->data_family},
          .refs_family = forge::db::core::family{value.blob->refs_family},
      };
   }
   if (value.revision) {
      options.revision = revision_layer_options{};
   }
   return options;
}

void validate_options(const store_options& value, const std::string& store_name, bool programmatic) {
   const auto fail = [&](const char* message, const std::string& family) {
      if (programmatic) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument, message, forge::exceptions::ctx("store", store_name),
                               forge::exceptions::ctx("family", family));
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, message, forge::exceptions::ctx("store", store_name),
                            forge::exceptions::ctx("family", family));
   };

   if (value.revision && !value.object) {
      if (programmatic) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "db store revision layer requires object layer",
                               forge::exceptions::ctx("store", store_name));
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store revision layer requires object layer",
                            forge::exceptions::ctx("store", store_name));
   }

   if (value.object && value.blob) {
      if (value.object->family.name == value.blob->data_family.name) {
         fail("db store object and blob data families must be distinct", value.object->family.name);
      }
      if (value.object->family.name == value.blob->refs_family.name) {
         fail("db store object and blob refs families must be distinct", value.object->family.name);
      }
   }
   if (value.blob && value.blob->data_family.name == value.blob->refs_family.name) {
      fail("db store blob data and refs families must be distinct", value.blob->data_family.name);
   }
}

void validate_config(const config& value) {
   auto names = std::unordered_set<std::string>{};
   for (const auto& item : value.stores) {
      if (item.name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store name must not be empty");
      }
      if (!names.insert(item.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store name is duplicated",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store driver must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver == "rocksdb" && item.mdbx) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store MDBX options require driver mdbx",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver == "mdbx") {
         validate_mdbx_options(item);
      }
      if (item.path.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store path must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (!item.object && !item.blob) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store must configure object or blob layer",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.revision && !item.object) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store revision layer requires object layer",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.object && item.object->family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store object family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.blob && item.blob->data_family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store blob data family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.blob && item.blob->refs_family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store blob refs family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      auto families = std::unordered_set<std::string>{};
      const auto add_family = [&](const std::string& family) {
         if (family.empty()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store family must not be empty",
                                  forge::exceptions::ctx("store", item.name));
         }
         if (!families.insert(family).second) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store family is duplicated",
                                  forge::exceptions::ctx("store", item.name), forge::exceptions::ctx("family", family));
         }
      };
      if (item.object) {
         add_family(item.object->family);
      }
      if (item.blob) {
         add_family(item.blob->data_family);
         add_family(item.blob->refs_family);
      }
      for (const auto& family : item.families) {
         add_family(family);
      }
      validate_options(parse_options(item), item.name, false);
   }
}

} // namespace forge::plugins::db::store::detail
