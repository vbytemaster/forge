module;

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module fcl.p2p.peer_store;

import fcl.p2p.dht;
import fcl.p2p.discovery;
import fcl.p2p.endpoint;
import fcl.p2p.identity;
import fcl.p2p.protocol;
import fcl.p2p.reachability;
import fcl.p2p.rendezvous;
import fcl.p2p.scoring;

export namespace fcl::p2p {

class peer_store {
 public:
   class backend;

   enum class backend_kind {
      memory,
      rocksdb,
   };

   struct rocksdb_options {
      std::filesystem::path path;
      bool create_if_missing = true;
      std::string key_prefix = "fcl.p2p.peer_store.v1";
   };

   struct endpoint_record {
      fcl::p2p::endpoint endpoint;
      path::kind kind = path::kind::direct;
      std::optional<peer_id> relay_peer;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      std::chrono::system_clock::time_point backoff_until{};
      double score = 0.0;
   };

   struct relay_record {
      peer_id relay;
      std::uint64_t reservation_id = 0;
      std::chrono::system_clock::time_point expires_at{};
      std::vector<fcl::p2p::endpoint> endpoints;
      std::vector<std::uint8_t> voucher;
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      double score = 0.0;
   };

   struct record {
      peer_id peer;
      capability_set capabilities{};
      discovery::source discovered_by = discovery::source::explicit_config;
      std::string protocol_version;
      std::string agent_version;
      std::vector<std::uint8_t> public_key;
      std::vector<protocol_id> protocols;
      std::vector<std::uint8_t> signed_peer_record;
      std::vector<endpoint_record> endpoints;
      std::vector<relay_record> relay_reservations;
      reachability::state reachability = reachability::state::unknown;
      std::optional<fcl::p2p::endpoint> observed_endpoint;
      std::chrono::system_clock::time_point reachability_expires_at{};
      std::chrono::system_clock::time_point discovered_at{};
      std::chrono::system_clock::time_point discovery_expires_at{};
      std::chrono::system_clock::time_point discovery_backoff_until{};
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
      std::chrono::milliseconds last_latency{0};
      double score = 0.0;
   };

   struct provider_record {
      dht::key key;
      dht::peer provider;
      discovery::source discovered_by = discovery::source::dht;
      std::chrono::system_clock::time_point expires_at{};
      std::uint64_t successes = 0;
      std::uint64_t failures = 0;
   };

   struct options {
      std::shared_ptr<backend> backend;
   };

   peer_store();
   explicit peer_store(options options_value);
   ~peer_store();

   peer_store(const peer_store&) = delete;
   peer_store& operator=(const peer_store&) = delete;

   peer_store(peer_store&&) noexcept;
   peer_store& operator=(peer_store&&) noexcept;

   [[nodiscard]] static std::shared_ptr<backend> make_memory_backend();
   [[nodiscard]] static std::shared_ptr<backend> make_rocksdb_backend(rocksdb_options options);

   void upsert(record value);
   void learn_endpoint(peer_id peer, fcl::p2p::endpoint endpoint, capability_set capabilities = {});
   void mark_reachability(peer_id peer, reachability::state state,
                          std::optional<fcl::p2p::endpoint> observed = std::nullopt);
   void mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency);
   void mark_failure(const peer_id& peer);
   void mark_endpoint_success(const peer_id& peer, const fcl::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::milliseconds latency);
   void mark_endpoint_failure(const peer_id& peer, const fcl::p2p::endpoint& endpoint, path::kind kind,
                              std::chrono::system_clock::time_point backoff_until);
   void upsert_routing_peer(dht::peer value, discovery::source source,
                            std::chrono::system_clock::time_point expires_at);
   void upsert_provider(provider_record value);
   void upsert_rendezvous(rendezvous::registration value);
   void remove_rendezvous(peer_id peer, std::string namespace_name);

   [[nodiscard]] std::optional<record> find(const peer_id& peer) const;
   [[nodiscard]] std::vector<record> snapshot() const;
   [[nodiscard]] std::vector<dht::peer> closest_routing_peers(const dht::key& key, std::size_t limit) const;
   [[nodiscard]] std::vector<provider_record> find_providers(const dht::key& key) const;
   [[nodiscard]] std::vector<rendezvous::registration>
   discover_rendezvous(std::string_view namespace_name, std::uint64_t after_sequence, std::size_t limit) const;

 private:
   struct impl;
   std::shared_ptr<impl> impl_;
};

class peer_store::backend {
 public:
   virtual ~backend() = default;

   virtual void upsert(record value) = 0;
   virtual void learn_endpoint(peer_id peer, fcl::p2p::endpoint endpoint, capability_set capabilities) = 0;
   virtual void mark_reachability(peer_id peer, reachability::state state,
                                  std::optional<fcl::p2p::endpoint> observed) = 0;
   virtual void mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) = 0;
   virtual void mark_failure(const peer_id& peer) = 0;
   virtual void mark_endpoint_success(const peer_id& peer, const fcl::p2p::endpoint& endpoint, path::kind kind,
                                      std::chrono::milliseconds latency) = 0;
   virtual void mark_endpoint_failure(const peer_id& peer, const fcl::p2p::endpoint& endpoint, path::kind kind,
                                      std::chrono::system_clock::time_point backoff_until) = 0;
   virtual void upsert_routing_peer(dht::peer value, discovery::source source,
                                    std::chrono::system_clock::time_point expires_at) = 0;
   virtual void upsert_provider(provider_record value) = 0;
   virtual void upsert_rendezvous(rendezvous::registration value) = 0;
   virtual void remove_rendezvous(peer_id peer, std::string namespace_name) = 0;
   [[nodiscard]] virtual std::optional<record> find(const peer_id& peer) const = 0;
   [[nodiscard]] virtual std::vector<record> snapshot() const = 0;
   [[nodiscard]] virtual std::vector<dht::peer> closest_routing_peers(const dht::key& key, std::size_t limit) const = 0;
   [[nodiscard]] virtual std::vector<provider_record> find_providers(const dht::key& key) const = 0;
   [[nodiscard]] virtual std::vector<rendezvous::registration>
   discover_rendezvous(std::string_view namespace_name, std::uint64_t after_sequence, std::size_t limit) const = 0;
};

} // namespace fcl::p2p
