module;

#include <boost/describe.hpp>

#include <cstdint>
#include <string>

export module fcl.plugins.http_server.types;

import fcl.schema.diagnostic;
import fcl.schema.value_kind;
import fcl.schema.object;
import fcl.schema.enums;

export namespace fcl::plugins::http_server {

struct config {
   std::string bind_address = "127.0.0.1";
   std::uint64_t port = 0;
   std::string api_base_path = "/";
   std::uint64_t max_request_body_bytes = 16 * 1024 * 1024;
   std::uint64_t max_header_bytes = 64 * 1024;
   std::uint64_t read_timeout_ms = 30'000;
   std::uint64_t idle_timeout_ms = 120'000;
};

struct publish_options {
   std::string base_path;
};

BOOST_DESCRIBE_STRUCT(config, (),
                      (bind_address, port, api_base_path, max_request_body_bytes, max_header_bytes, read_timeout_ms,
                       idle_timeout_ms))
BOOST_DESCRIBE_STRUCT(publish_options, (), (base_path))

} // namespace fcl::plugins::http_server

export template <> struct fcl::schema::rules<fcl::plugins::http_server::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::http_server::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::http_server::config>();
      schema.field<&fcl::plugins::http_server::config::bind_address>("bind-address")
         .default_value("127.0.0.1")
         .description("HTTP server bind address");
      schema.field<&fcl::plugins::http_server::config::port>("port")
         .default_value(std::uint64_t{0})
         .range(0, 65'535)
         .description("HTTP server TCP port; 0 asks the OS to choose an available port");
      schema.field<&fcl::plugins::http_server::config::api_base_path>("api-base-path")
         .default_value("/")
         .description("Default base path for typed HTTP APIs");
      schema.field<&fcl::plugins::http_server::config::max_request_body_bytes>("max-request-body-bytes")
         .default_value(std::uint64_t{16 * 1024 * 1024})
         .range(1, 1024ULL * 1024ULL * 1024ULL);
      schema.field<&fcl::plugins::http_server::config::max_header_bytes>("max-header-bytes")
         .default_value(std::uint64_t{64 * 1024})
         .range(1, 16ULL * 1024ULL * 1024ULL);
      schema.field<&fcl::plugins::http_server::config::read_timeout_ms>("read-timeout-ms")
         .default_value(std::uint64_t{30'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::http_server::config::idle_timeout_ms>("idle-timeout-ms")
         .default_value(std::uint64_t{120'000})
         .range(1, 86'400'000);
      return schema;
   }
};
