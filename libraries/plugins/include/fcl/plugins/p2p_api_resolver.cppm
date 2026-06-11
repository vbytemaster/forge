module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <fcl/api/api_macros.hpp>
#include <fcl/exceptions/macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module fcl.plugins.p2p_api_resolver;

import fcl.api;
import fcl.api.transport;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.app.plugin_registry;
import fcl.config.component;
import fcl.exceptions;
import fcl.p2p;
import fcl.plugins.p2p_node;
import fcl.schema;

export namespace fcl::plugins::p2p_api_resolver {

struct config;
struct publish_options;
struct resolve_options;
struct error;
struct method;
struct entry;
struct resolution;
struct resolved_connection;
struct query;
struct response;
class exceptions;
class api;

class plugin final : public fcl::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] fcl::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<fcl::config::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(fcl::config::component_view view) override;
   boost::asio::awaitable<void> provide(fcl::api::provider& provider) override;
   boost::asio::awaitable<void> initialize(fcl::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class protocol_impl;
   class api_impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] fcl::app::plugin_descriptor descriptor();
[[nodiscard]] fcl::p2p::protocol_id default_protocol();

class exceptions {
 public:
   enum class code : std::uint16_t {
      plugin_not_initialized = 1,
      invalid_config = 2,
      duplicate_api = 3,
      not_found = 4,
      incompatible_api = 5,
      protocol_error = 6,
   };

   using plugin_not_initialized = fcl::exceptions::coded_exception<code, code::plugin_not_initialized>;
   using invalid_config = fcl::exceptions::coded_exception<code, code::invalid_config>;
   using duplicate_api = fcl::exceptions::coded_exception<code, code::duplicate_api>;
   using not_found = fcl::exceptions::coded_exception<code, code::not_found>;
   using incompatible_api = fcl::exceptions::coded_exception<code, code::incompatible_api>;
   using protocol_error = fcl::exceptions::coded_exception<code, code::protocol_error>;
};

FCL_DECLARE_EXCEPTION_CATEGORY(exceptions::code, "fcl.plugins.p2p_api_resolver")

struct config {
   std::string protocol_id = "/fcl/api/resolver/1";
   std::uint64_t cache_ttl_ms = 60'000;
   std::uint64_t query_deadline_ms = 5'000;
   std::uint64_t open_deadline_ms = 10'000;
   std::uint64_t max_cached_peers = 4'096;
   std::uint64_t max_apis_per_peer = 1'024;
   std::uint64_t max_methods_per_api = 256;
   std::uint64_t max_errors_per_method = 64;
};

struct publish_options {
   fcl::api::transport::options transport{};
};

struct resolve_options {
   std::chrono::milliseconds query_deadline{0};
   std::chrono::milliseconds open_deadline{0};
   bool force_refresh = false;
};

struct error {
   std::string name;
   fcl::api::error_identity identity;
   fcl::api::status status_code = fcl::api::status::internal;
   bool retryable = false;

   bool operator==(const error&) const = default;
};

struct method {
   std::string name;
   fcl::api::method_kind kind = fcl::api::method_kind::unary;
   std::vector<error> errors;

   bool operator==(const method&) const = default;
};

struct entry {
   fcl::api::api_id id;
   fcl::api::api_version version;
   std::string protocol;
   fcl::api::codec_id codec{.value = "fcl.raw"};
   std::uint64_t max_inflight = 0;
   std::uint64_t max_frame_size = 0;
   std::vector<method> methods;

   bool operator==(const entry&) const = default;
};

struct resolution {
   entry api;

   bool operator==(const resolution&) const = default;
};

struct resolved_connection {
   fcl::api::transport::connection connection;
   fcl::api::api_ref selected;
};

struct query {
   std::vector<fcl::api::api_ref> apis;

   bool operator==(const query&) const = default;
};

struct response {
   std::vector<entry> apis;

   bool operator==(const response&) const = default;
};

class api : public fcl::api::contract<api> {
 public:
   virtual ~api() = default;

   virtual void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol,
                            publish_options options = {}) = 0;
   [[nodiscard]] virtual std::vector<entry> local_apis() const = 0;
   virtual boost::asio::awaitable<std::vector<entry>> peer_apis(fcl::p2p::peer_id peer,
                                                                resolve_options options = {}) = 0;
   virtual boost::asio::awaitable<resolution> resolve(fcl::p2p::peer_id peer, fcl::api::api_ref api,
                                                      resolve_options options = {}) = 0;
   template <typename Interface>
   boost::asio::awaitable<fcl::api::handle<Interface>> remote(fcl::p2p::peer_id peer, resolve_options options = {}) {
      auto descriptor = Interface::describe();
      auto requested = fcl::api::api_ref{.id = descriptor.id,
                                         .major = descriptor.version.major,
                                         .min_revision = descriptor.version.revision};
      auto resolved =
         co_await open_resolved_connection(std::move(peer), std::move(requested), std::move(descriptor), options);
      co_return co_await resolved.connection.template get_remote_api<Interface>(std::move(resolved.selected));
   }

 private:
   friend class plugin;

   virtual boost::asio::awaitable<resolved_connection>
   open_resolved_connection(fcl::p2p::peer_id peer, fcl::api::api_ref api, fcl::api::descriptor descriptor,
                            resolve_options options) = 0;
};

BOOST_DESCRIBE_STRUCT(config, (),
                      (protocol_id, cache_ttl_ms, query_deadline_ms, open_deadline_ms, max_cached_peers,
                       max_apis_per_peer, max_methods_per_api, max_errors_per_method))
BOOST_DESCRIBE_STRUCT(error, (), (name, identity, status_code, retryable))
BOOST_DESCRIBE_STRUCT(method, (), (name, kind, errors))
BOOST_DESCRIBE_STRUCT(entry, (),
                      (id, version, protocol, codec, max_inflight, max_frame_size, methods))
BOOST_DESCRIBE_STRUCT(query, (), (apis))
BOOST_DESCRIBE_STRUCT(response, (), (apis))

} // namespace fcl::plugins::p2p_api_resolver

export {
FCL_API(::fcl::plugins::p2p_api_resolver::api, FCL_API_CONTRACT("fcl.plugins.p2p_api_resolver", 1, 0))
}

export template <> struct fcl::schema::rules<fcl::plugins::p2p_api_resolver::config> {
   [[nodiscard]] static fcl::schema::object_schema<fcl::plugins::p2p_api_resolver::config> define() {
      auto schema = fcl::schema::object<fcl::plugins::p2p_api_resolver::config>();
      schema.field<&fcl::plugins::p2p_api_resolver::config::protocol_id>("protocol-id")
         .default_value("/fcl/api/resolver/1")
         .description("P2P protocol id used for FCL API metadata resolution");
      schema.field<&fcl::plugins::p2p_api_resolver::config::cache_ttl_ms>("cache-ttl-ms")
         .default_value(std::uint64_t{60'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::query_deadline_ms>("query-deadline-ms")
         .default_value(std::uint64_t{5'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::open_deadline_ms>("open-deadline-ms")
         .default_value(std::uint64_t{10'000})
         .range(1, 86'400'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_cached_peers>("max-cached-peers")
         .default_value(std::uint64_t{4'096})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_apis_per_peer>("max-apis-per-peer")
         .default_value(std::uint64_t{1'024})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_methods_per_api>("max-methods-per-api")
         .default_value(std::uint64_t{256})
         .range(1, 1'000'000);
      schema.field<&fcl::plugins::p2p_api_resolver::config::max_errors_per_method>("max-errors-per-method")
         .default_value(std::uint64_t{64})
         .range(0, 1'000'000);
      return schema;
   }
};
