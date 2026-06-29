#pragma once

namespace forge::p2p::dht_query {

struct request {
   dht::key target;
   std::optional<peer_id> target_peer;
   dht::options options;
   std::vector<dht::peer> seeds;
};

struct result {
   dht::query_result query;
   std::vector<peer_id> queried;
   std::vector<peer_id> failed;
};

[[nodiscard]] inline bool has_endpoint(const dht::peer& value) noexcept {
   return !value.endpoints.empty();
}

inline void merge_peer(dht::peer& target, const dht::peer& source) {
   if (target.id.value.empty()) {
      target.id = source.id;
   }
   target.connection = source.connection;
   for (const auto& endpoint : source.endpoints) {
      const auto exists = std::ranges::any_of(target.endpoints, [&](const auto& current) {
         return current.to_string() == endpoint.to_string();
      });
      if (!exists) {
         target.endpoints.push_back(endpoint);
      }
   }
}

inline void merge_known(std::map<peer_id, dht::peer>& known, const dht::peer& value) {
   if (!valid_peer_id(value.id)) {
      return;
   }
   auto& item = known[value.id];
   merge_peer(item, value);
}

inline void merge_provider(std::vector<dht::peer>& providers, const dht::peer& value) {
   if (!valid_peer_id(value.id)) {
      return;
   }
   const auto found = std::ranges::find_if(providers, [&](const auto& current) {
      return current.id == value.id;
   });
   if (found == providers.end()) {
      providers.push_back(value);
      return;
   }
   merge_peer(*found, value);
}

[[nodiscard]] inline std::vector<dht::peer> sorted_peers(const std::map<peer_id, dht::peer>& known,
                                                         const dht::key& target) {
   auto out = std::vector<dht::peer>{};
   out.reserve(known.size());
   for (const auto& [_, peer] : known) {
      out.push_back(peer);
   }
   std::ranges::sort(out, [&](const auto& left, const auto& right) {
      const auto left_distance = distance_between(left.id.to_bytes(), target.bytes);
      const auto right_distance = distance_between(right.id.to_bytes(), target.bytes);
      if (left_distance != right_distance) {
         return left_distance < right_distance;
      }
      return left.id.to_string() < right.id.to_string();
   });
   return out;
}

[[nodiscard]] inline std::vector<dht::peer> next_batch(const std::map<peer_id, dht::peer>& known,
                                                       const std::set<peer_id>& queried,
                                                       const std::set<peer_id>& failed,
                                                       const dht::key& target,
                                                       std::size_t alpha) {
   auto out = std::vector<dht::peer>{};
   if (alpha == 0) {
      return out;
   }
   for (const auto& peer : sorted_peers(known, target)) {
      if (out.size() >= alpha) {
         break;
      }
      if (!has_endpoint(peer) || queried.contains(peer.id) || failed.contains(peer.id)) {
         continue;
      }
      out.push_back(peer);
   }
   return out;
}

struct batch_response {
   dht::peer peer;
   std::optional<dht::message> response;
   bool failed = false;
};

template <typename Query>
boost::asio::awaitable<std::vector<batch_response>>
query_batch_on_strand(std::vector<dht::peer> batch, Query& query,
                      boost::asio::strand<boost::asio::any_io_executor> strand) {
   namespace asio = boost::asio;

   struct state {
      explicit state(asio::strand<asio::any_io_executor> executor, std::vector<dht::peer> peers)
          : remaining(peers.size()), timer(std::move(executor)) {
         items.reserve(peers.size());
         for (auto& peer : peers) {
            items.push_back(batch_response{.peer = std::move(peer)});
         }
      }

      std::vector<batch_response> items;
      std::size_t remaining;
      asio::steady_timer timer;
   };

   auto shared = std::make_shared<state>(strand, std::move(batch));
   for (auto index = std::size_t{}; index < shared->items.size(); ++index) {
      const auto peer = shared->items[index].peer;
      asio::co_spawn(
          strand,
          [shared, index, peer, &query]() mutable -> asio::awaitable<void> {
             auto response = std::optional<dht::message>{};
             auto failed = false;
             try {
                response = co_await query(peer);
             } catch (const forge::exceptions::base&) {
                failed = true;
             }
             shared->items[index].response = std::move(response);
             shared->items[index].failed = failed;
             --shared->remaining;
             shared->timer.cancel(); // on_strand
          },
          asio::detached);
   }

   while (true) {
      if (shared->remaining == 0) {
         co_return std::move(shared->items);
      }
      shared->timer.expires_after(std::chrono::minutes{10});
      auto error = boost::system::error_code{};
      co_await shared->timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
   }
}

template <typename Query>
boost::asio::awaitable<std::vector<batch_response>> query_batch(std::vector<dht::peer> batch, Query& query) {
   namespace asio = boost::asio;
   auto executor = asio::any_io_executor{co_await asio::this_coro::executor};
   auto strand = asio::make_strand(executor);
   co_return co_await asio::co_spawn(
       strand,
       [batch = std::move(batch), &query, strand]() mutable -> asio::awaitable<std::vector<batch_response>> {
          co_return co_await query_batch_on_strand(std::move(batch), query, strand);
       },
       asio::use_awaitable);
}

template <typename Query>
boost::asio::awaitable<result> run(request value, Query&& query) {
   auto known = std::map<peer_id, dht::peer>{};
   for (const auto& peer : value.seeds) {
      merge_known(known, peer);
   }

   auto queried = std::set<peer_id>{};
   auto failed = std::set<peer_id>{};
   auto out = result{.query = dht::query_result{.target = value.target}};
   const auto alpha = std::max<std::size_t>(1, value.options.alpha);
   const auto max_seen =
       std::max({value.options.replication, value.options.max_closer_peers, value.options.max_provider_peers, alpha}) *
       8U;

   while (queried.size() + failed.size() < max_seen) {
      const auto batch = next_batch(known, queried, failed, value.target, alpha);
      if (batch.empty()) {
         break;
      }

      for (const auto& peer : batch) {
         queried.insert(peer.id);
      }
      auto responses = co_await query_batch(std::move(batch), query);
      for (const auto& item : responses) {
         if (item.failed || !item.response) {
            failed.insert(item.peer.id);
            continue;
         }
         for (const auto& closer : item.response->closer_peers) {
            merge_known(known, closer);
            if (value.target_peer && closer.id == *value.target_peer) {
               out.query.complete = true;
            }
         }
         for (const auto& provider : item.response->provider_peers) {
            merge_provider(out.query.provider_peers, provider);
         }
      }

      if (out.query.complete || !out.query.provider_peers.empty()) {
         break;
      }
   }

   auto closest = sorted_peers(known, value.target);
   if (closest.size() > value.options.replication) {
      closest.resize(value.options.replication);
   }
   out.query.closest_peers = std::move(closest);
   out.queried.assign(queried.begin(), queried.end());
   out.failed.assign(failed.begin(), failed.end());
   co_return out;
}

} // namespace forge::p2p::dht_query
