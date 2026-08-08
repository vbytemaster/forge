#pragma once

namespace forge::plugins::p2p::resolver {

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   struct cache_record {
      std::vector<entry> apis;
      std::chrono::steady_clock::time_point expires_at;
      std::chrono::steady_clock::time_point stored_at;
   };

   mutable std::mutex mutex;
   config settings;
   forge::api::transport::options resolver_transport{};
   forge::net::p2p::protocol_id protocol = default_protocol();
   forge::plugins::p2p::node::api* p2p = nullptr;
   forge::api::core::registry protocol_registry;
   std::vector<entry> local;
   std::map<std::string, cache_record> cache;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] forge::plugins::p2p::node::api& require_p2p() const;
   [[nodiscard]] std::chrono::milliseconds query_deadline(resolve_options value) const;
   [[nodiscard]] std::chrono::milliseconds open_deadline(resolve_options value) const;
   [[nodiscard]] std::chrono::milliseconds request_deadline(resolve_options value) const;
   void evict_cache_locked();
   [[nodiscard]] std::optional<std::vector<entry>> cached_peer(const forge::net::p2p::peer_id& peer,
                                                              resolve_options options) const;
   void store_peer(const forge::net::p2p::peer_id& peer, std::vector<entry> entries);
   [[nodiscard]] std::vector<entry> local_snapshot() const;
   void add_local(forge::api::core::binding_plan plan, forge::net::p2p::protocol_id route, publish_options options);
   [[nodiscard]] response query_local(const query& request) const;
   [[nodiscard]] static std::string api_key(const forge::api::core::api_id& id, std::uint16_t major);
   [[nodiscard]] entry project_descriptor(const forge::api::core::descriptor& descriptor,
                                          const forge::net::p2p::protocol_id& protocol,
                                          const forge::api::transport::options& options) const;
   void validate_entry(const entry& value, std::string_view source) const;
   void validate_response(const std::vector<entry>& entries) const;
   void validate_descriptor_compatible(const forge::api::core::descriptor& descriptor,
                                       const entry& remote) const;
   [[nodiscard]] std::optional<entry> select_compatible(
      const std::vector<entry>& entries,
      const forge::api::core::api_ref& requested) const;
   void install_protocol();
   boost::asio::awaitable<std::vector<entry>> query_remote_apis(forge::net::p2p::peer_id peer,
                                                                resolve_options options);
};

} // namespace forge::plugins::p2p::resolver
