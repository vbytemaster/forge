module;

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

export module forge.net.p2p.topology;

import forge.net.p2p.endpoint;

export namespace forge::net::p2p {

struct topology {
   enum class mode {
      managed,
      static_only,
   };

   struct watermarks {
      std::size_t low = 128;
      std::size_t target = 160;
      std::size_t high = 192;
   };

   struct rendezvous_point {
      forge::net::p2p::endpoint endpoint;
      std::vector<std::string> namespaces;
   };

   struct policy {
      mode operating_mode = mode::managed;
      watermarks peers{};
      std::chrono::milliseconds refresh_interval{600'000};
      std::chrono::milliseconds query_timeout{10'000};
      std::size_t max_candidates = 256;
      std::size_t max_parallel_queries = 10;
      std::size_t max_parallel_dials = 4;
      std::size_t max_rendezvous_points = 4;
      std::size_t max_rendezvous_namespaces = 16;
      std::size_t max_peer_exchange_peers = 4;
      std::size_t max_tagged_peers = 1024;
      std::size_t max_tags_per_peer = 16;
      std::size_t max_tag_size = 128;
      double retry_jitter = 0.20;
      bool dht_enabled = true;
      bool rendezvous_enabled = true;
      bool peer_exchange_enabled = true;
      std::vector<rendezvous_point> rendezvous_points;
   };
};

void validate(const topology::policy& policy);

} // namespace forge::net::p2p
