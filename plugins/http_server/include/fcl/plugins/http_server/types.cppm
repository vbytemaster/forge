module;

#include <boost/describe.hpp>

#include <chrono>
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
   std::uint64_t port = 8080;
   std::string api_base_path = "/api/v1";
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
         .description("HTTP listen address");
      schema.field<&fcl::plugins::http_server::config::port>("port")
         .default_value(std::uint64_t{8080})
         .range(1, 65535)
         .description("HTTP listen port");
      schema.field<&fcl::plugins::http_server::config::api_base_path>("api-base-path")
         .default_value("/api/v1")
         .description("Default base path used for published typed HTTP APIs");
      schema.field<&fcl::plugins::http_server::config::max_request_body_bytes>("max-request-body-bytes")
         .default_value(std::uint64_t{16 * 1024 * 1024})
         .range(1, 1024ULL * 1024ULL * 1024ULL)
         .description("Maximum accepted HTTP request body size in bytes");
      schema.field<&fcl::plugins::http_server::config::max_header_bytes>("max-header-bytes")
         .default_value(std::uint64_t{64 * 1024})
         .range(1, 16ULL * 1024ULL * 1024ULL)
         .description("Maximum accepted HTTP header size in bytes");
      schema.field<&fcl::plugins::http_server::config::read_timeout_ms>("read-timeout-ms")
         .default_value(std::uint64_t{30'000})
         .range(1, 3'600'000)
         .description("Per-request read timeout in milliseconds");
      schema.field<&fcl::plugins::http_server::config::idle_timeout_ms>("idle-timeout-ms")
         .default_value(std::uint64_t{120'000})
         .range(1, 3'600'000)
         .description("Idle timeout in milliseconds");
      return schema;
   }
};
