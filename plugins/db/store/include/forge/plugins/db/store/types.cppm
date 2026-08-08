module;

#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module forge.plugins.db.store.types;

import forge.db.blob.store;
import forge.db.core.record;
import forge.db.object.store;
import forge.schema.object;
import forge.schema.value_kind;

export namespace forge::plugins::db::store {

inline constexpr std::uint64_t default_blob_file_size = 256ULL * 1024ULL * 1024ULL;

struct object_layer_config {
   std::string family = "objectdb";
   std::string write_policy = "single-writer";
   std::string id_allocation = "monotonic";
};

struct blob_data_options {
   bool enable_blob_files = false;
   std::uint64_t min_blob_size = 0;
   std::uint64_t blob_file_size = default_blob_file_size;
   std::string blob_compression_type = "none";
   bool enable_blob_garbage_collection = false;
   double blob_garbage_collection_age_cutoff = 0.25;
};

struct blob_layer_config {
   std::string data_family = "blobdb.data";
   std::string refs_family = "blobdb.refs";
   blob_data_options data_blobs;
};

struct revision_layer_config {};

struct mdbx_map_config {
   std::optional<std::uint64_t> lower_size;
   std::optional<std::uint64_t> current_size;
   std::optional<std::uint64_t> upper_size;
   std::optional<std::uint64_t> growth_step;
   std::optional<std::uint64_t> shrink_threshold;
   std::optional<std::uint32_t> page_size;
};

struct mdbx_lane_config {
   std::size_t max_pending_operations = 1024;
   std::size_t max_waiting_submissions = 1024;
   std::optional<std::string> thread_name;
};

struct mdbx_driver_config {
   std::string durability = "durable-sync";
   std::size_t max_readers = 128;
   mdbx_map_config map;
   mdbx_lane_config lane;
};

struct store_config {
   std::string name;
   std::string driver = "rocksdb";
   std::string path;
   std::vector<std::string> families;
   std::optional<object_layer_config> object;
   std::optional<blob_layer_config> blob;
   std::optional<revision_layer_config> revision;
   std::optional<mdbx_driver_config> mdbx;
   bool create_if_missing = true;
   bool create_missing_column_families = true;
};

struct object_layer_options {
   forge::db::core::family family{"objectdb"};
   forge::db::object::store::options runtime;
};

struct blob_layer_options {
   forge::db::core::family data_family{"blobdb.data"};
   forge::db::core::family refs_family{"blobdb.refs"};
};

struct revision_layer_options {};

struct store_options {
   std::optional<object_layer_options> object = object_layer_options{};
   std::optional<blob_layer_options> blob;
   std::optional<revision_layer_options> revision;
};

struct config {
   std::vector<store_config> stores;
};

struct store_status {
   std::string name;
   std::string driver;
   std::optional<std::string> durability;
   std::string path;
   std::vector<std::string> families;
   bool object = false;
   bool blob = false;
   bool revision = false;
   bool started = false;
};

struct status {
   std::vector<store_status> stores;
};

BOOST_DESCRIBE_STRUCT(object_layer_config, (), (family, write_policy, id_allocation))
BOOST_DESCRIBE_STRUCT(blob_data_options, (),
                      (enable_blob_files, min_blob_size, blob_file_size, blob_compression_type,
                       enable_blob_garbage_collection, blob_garbage_collection_age_cutoff))
BOOST_DESCRIBE_STRUCT(blob_layer_config, (), (data_family, refs_family, data_blobs))
BOOST_DESCRIBE_STRUCT(revision_layer_config, (), ())
BOOST_DESCRIBE_STRUCT(mdbx_map_config, (),
                      (lower_size, current_size, upper_size, growth_step, shrink_threshold, page_size))
BOOST_DESCRIBE_STRUCT(mdbx_lane_config, (), (max_pending_operations, max_waiting_submissions, thread_name))
BOOST_DESCRIBE_STRUCT(mdbx_driver_config, (), (durability, max_readers, map, lane))
BOOST_DESCRIBE_STRUCT(store_config, (),
                      (name, driver, path, families, object, blob, revision, mdbx, create_if_missing,
                       create_missing_column_families))
BOOST_DESCRIBE_STRUCT(config, (), (stores))
BOOST_DESCRIBE_STRUCT(store_status, (), (name, driver, durability, path, families, object, blob, revision, started))
BOOST_DESCRIBE_STRUCT(status, (), (stores))

} // namespace forge::plugins::db::store

export template <> struct forge::schema::rules<forge::plugins::db::store::object_layer_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::object_layer_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::object_layer_config>();
      schema.field<&forge::plugins::db::store::object_layer_config::family>("family")
          .default_value("objectdb")
          .non_empty();
      schema.field<&forge::plugins::db::store::object_layer_config::write_policy>("write-policy")
          .default_value("single-writer")
          .non_empty();
      schema.field<&forge::plugins::db::store::object_layer_config::id_allocation>("id-allocation")
          .default_value("monotonic")
          .non_empty();
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::blob_data_options> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::blob_data_options> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::blob_data_options>();
      schema.field<&forge::plugins::db::store::blob_data_options::enable_blob_files>("enable-blob-files")
          .default_value(false);
      schema.field<&forge::plugins::db::store::blob_data_options::min_blob_size>("min-blob-size")
          .default_value(std::uint64_t{0});
      schema.field<&forge::plugins::db::store::blob_data_options::blob_file_size>("blob-file-size")
          .default_value(forge::plugins::db::store::default_blob_file_size);
      schema.field<&forge::plugins::db::store::blob_data_options::blob_compression_type>("blob-compression-type")
          .default_value("none")
          .non_empty();
      schema
          .field<&forge::plugins::db::store::blob_data_options::enable_blob_garbage_collection>(
              "enable-blob-garbage-collection")
          .default_value(false);
      schema
          .field<&forge::plugins::db::store::blob_data_options::blob_garbage_collection_age_cutoff>(
              "blob-garbage-collection-age-cutoff")
          .default_value(0.25)
          .range(0.0, 1.0);
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::blob_layer_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::blob_layer_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::blob_layer_config>();
      schema.field<&forge::plugins::db::store::blob_layer_config::data_family>("data-family")
          .default_value("blobdb.data")
          .non_empty();
      schema.field<&forge::plugins::db::store::blob_layer_config::refs_family>("refs-family")
          .default_value("blobdb.refs")
          .non_empty();
      schema.field<&forge::plugins::db::store::blob_layer_config::data_blobs>("data-blobs")
          .default_value(forge::plugins::db::store::blob_data_options{});
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::revision_layer_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::revision_layer_config> define() {
      return forge::schema::object<forge::plugins::db::store::revision_layer_config>();
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::mdbx_map_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::mdbx_map_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::mdbx_map_config>();
      schema.field<&forge::plugins::db::store::mdbx_map_config::lower_size>("lower-size");
      schema.field<&forge::plugins::db::store::mdbx_map_config::current_size>("current-size");
      schema.field<&forge::plugins::db::store::mdbx_map_config::upper_size>("upper-size");
      schema.field<&forge::plugins::db::store::mdbx_map_config::growth_step>("growth-step");
      schema.field<&forge::plugins::db::store::mdbx_map_config::shrink_threshold>("shrink-threshold");
      schema.field<&forge::plugins::db::store::mdbx_map_config::page_size>("page-size");
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::mdbx_lane_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::mdbx_lane_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::mdbx_lane_config>();
      schema.field<&forge::plugins::db::store::mdbx_lane_config::max_pending_operations>("max-pending-operations")
          .default_value(std::size_t{1024});
      schema.field<&forge::plugins::db::store::mdbx_lane_config::max_waiting_submissions>("max-waiting-submissions")
          .default_value(std::size_t{1024});
      schema.field<&forge::plugins::db::store::mdbx_lane_config::thread_name>("thread-name");
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::mdbx_driver_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::mdbx_driver_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::mdbx_driver_config>();
      schema.field<&forge::plugins::db::store::mdbx_driver_config::durability>("durability")
          .default_value("durable-sync")
          .non_empty();
      schema.field<&forge::plugins::db::store::mdbx_driver_config::max_readers>("max-readers")
          .default_value(std::size_t{128});
      schema.field<&forge::plugins::db::store::mdbx_driver_config::map>("map").default_value(
          forge::plugins::db::store::mdbx_map_config{});
      schema.field<&forge::plugins::db::store::mdbx_driver_config::lane>("lane").default_value(
          forge::plugins::db::store::mdbx_lane_config{});
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::store_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::store_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::store_config>();
      schema.field<&forge::plugins::db::store::store_config::name>("name").required().non_empty();
      schema.field<&forge::plugins::db::store::store_config::driver>("driver").default_value("rocksdb").non_empty();
      schema.field<&forge::plugins::db::store::store_config::path>("path").required().non_empty();
      schema.field<&forge::plugins::db::store::store_config::families>("families").items<std::string>();
      schema.field<&forge::plugins::db::store::store_config::object>("object");
      schema.field<&forge::plugins::db::store::store_config::blob>("blob");
      schema.field<&forge::plugins::db::store::store_config::revision>("revision");
      schema.field<&forge::plugins::db::store::store_config::mdbx>("mdbx");
      schema.field<&forge::plugins::db::store::store_config::create_if_missing>("create-if-missing")
          .default_value(true);
      schema
          .field<&forge::plugins::db::store::store_config::create_missing_column_families>(
              "create-missing-column-families")
          .default_value(true);
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::store::config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::store::config> define() {
      auto schema = forge::schema::object<forge::plugins::db::store::config>();
      schema.field<&forge::plugins::db::store::config::stores>("stores")
          .items<forge::plugins::db::store::store_config>()
          .unique_by<&forge::plugins::db::store::store_config::name>();
      return schema;
   }
};
