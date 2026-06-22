#pragma once

namespace fcl::plugins::p2p::resolver {

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   struct cache_record {
      std::vector<entry> apis;
      std::chrono::steady_clock::time_point expires_at;
      std::chrono::steady_clock::time_point stored_at;
   };

   mutable std::mutex mutex;
   config settings;
   fcl::transport::api::options resolver_transport{};
   fcl::p2p::protocol_id protocol = default_protocol();
   fcl::plugins::p2p::node::api* p2p = nullptr;
   fcl::api::registry protocol_registry;
   std::vector<entry> local;
   std::map<std::string, cache_record> cache;
   bool initialized = false;
   bool stopping = false;

   [[nodiscard]] fcl::plugins::p2p::node::api& require_p2p() const;
   [[nodiscard]] std::chrono::milliseconds query_deadline(resolve_options value) const;
   [[nodiscard]] std::chrono::milliseconds open_deadline(resolve_options value) const;
   void evict_cache_locked();
   [[nodiscard]] std::optional<std::vector<entry>> cached_peer(const fcl::p2p::peer_id& peer,
                                                              resolve_options options) const;
   void store_peer(const fcl::p2p::peer_id& peer, std::vector<entry> entries);
   [[nodiscard]] std::vector<entry> local_snapshot() const;
   void add_local(fcl::api::binding_plan plan, fcl::p2p::protocol_id route, publish_options options);
   [[nodiscard]] response query_local(const query& request) const;
   void install_protocol();
   boost::asio::awaitable<std::vector<entry>> query_remote_apis(fcl::p2p::peer_id peer,
                                                                resolve_options options);
};

} // namespace fcl::plugins::p2p::resolver
