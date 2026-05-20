module;

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

module fcl.p2p.peer_store;

namespace fcl::p2p {
namespace {

[[nodiscard]] bool same_endpoint(const fcl::quic::endpoint& left, const fcl::quic::endpoint& right) {
   return left.host == right.host && left.port == right.port;
}

void refresh_endpoint_score(peer_store::endpoint_record& endpoint) {
   endpoint.score = score_path(path::observation{
       .kind = endpoint.kind,
       .latency = endpoint.last_latency,
       .failures = endpoint.failures,
       .successes = endpoint.successes,
       .last_success = endpoint.successes > 0 && endpoint.failures == 0,
   });
}

void expire_reachability(peer_store::record& record, std::chrono::steady_clock::time_point now) {
   if (record.reachability_expires_at == std::chrono::steady_clock::time_point{} ||
       record.reachability_expires_at > now) {
      return;
   }
   record.reachability = reachability_state::unknown;
   record.observed_endpoint.reset();
   record.reachability_expires_at = {};
}

} // namespace

struct peer_store::impl {
   mutable std::mutex mutex;
   std::map<peer_id, peer_store::record> records;
};

peer_store::peer_store() : impl_(std::make_shared<impl>()) {}

peer_store::~peer_store() = default;
peer_store::peer_store(peer_store&&) noexcept = default;
peer_store& peer_store::operator=(peer_store&&) noexcept = default;

void peer_store::upsert(record value) {
   auto lock = std::scoped_lock{impl_->mutex};
   value.score = score_path(path::observation{
       .kind = value.endpoints.empty() ? path::kind::direct : value.endpoints.front().kind,
       .latency = value.last_latency,
       .failures = value.failures,
       .successes = value.successes,
       .last_success = value.successes > 0,
   });
   impl_->records[value.peer] = std::move(value);
}

void peer_store::learn_endpoint(peer_id peer, fcl::quic::endpoint endpoint, capability_set capabilities) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = std::move(peer);
   record.capabilities.bits |= capabilities.bits;
   const auto exists = std::ranges::any_of(record.endpoints, [&](const endpoint_record& current) {
      return same_endpoint(current.endpoint, endpoint);
   });
   if (!exists) {
      auto entry = endpoint_record{.endpoint = std::move(endpoint)};
      refresh_endpoint_score(entry);
      record.endpoints.push_back(std::move(entry));
   }
}

void peer_store::mark_reachability(peer_id peer, reachability_state state,
                                   std::optional<fcl::quic::endpoint> observed) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = std::move(peer);
   record.reachability = state;
   record.observed_endpoint = std::move(observed);
   record.reachability_expires_at = std::chrono::steady_clock::now() + std::chrono::minutes{5};
}

void peer_store::mark_success(const peer_id& peer, path::kind kind, std::chrono::milliseconds latency) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = peer;
   ++record.successes;
   record.last_latency = latency;
   record.score = score_path(path::observation{
       .kind = kind,
       .latency = latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = true,
   });
}

void peer_store::mark_failure(const peer_id& peer) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = peer;
   ++record.failures;
   record.score = score_path(path::observation{
       .kind = record.endpoints.empty() ? path::kind::direct : record.endpoints.front().kind,
       .latency = record.last_latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = false,
   });
}

void peer_store::mark_endpoint_success(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                                       std::chrono::milliseconds latency) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = peer;
   auto iterator = std::ranges::find_if(record.endpoints,
                                        [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
   if (iterator == record.endpoints.end()) {
      iterator = record.endpoints.insert(record.endpoints.end(), endpoint_record{.endpoint = endpoint, .kind = kind});
   }
   iterator->kind = kind;
   iterator->last_latency = latency;
   iterator->backoff_until = {};
   ++iterator->successes;
   refresh_endpoint_score(*iterator);
   ++record.successes;
   record.last_latency = latency;
   record.score = score_path(path::observation{
       .kind = kind,
       .latency = latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = true,
   });
}

void peer_store::mark_endpoint_failure(const peer_id& peer, const fcl::quic::endpoint& endpoint, path::kind kind,
                                       std::chrono::steady_clock::time_point backoff_until) {
   auto lock = std::scoped_lock{impl_->mutex};
   auto& record = impl_->records[peer];
   record.peer = peer;
   auto iterator = std::ranges::find_if(record.endpoints,
                                        [&](const auto& current) { return same_endpoint(current.endpoint, endpoint); });
   if (iterator == record.endpoints.end()) {
      iterator = record.endpoints.insert(record.endpoints.end(), endpoint_record{.endpoint = endpoint, .kind = kind});
   }
   iterator->kind = kind;
   iterator->backoff_until = backoff_until;
   ++iterator->failures;
   refresh_endpoint_score(*iterator);
   ++record.failures;
   record.score = score_path(path::observation{
       .kind = kind,
       .latency = record.last_latency,
       .failures = record.failures,
       .successes = record.successes,
       .last_success = false,
   });
}

std::optional<peer_store::record> peer_store::find(const peer_id& peer) const {
   auto lock = std::scoped_lock{impl_->mutex};
   const auto it = impl_->records.find(peer);
   if (it == impl_->records.end()) {
      return std::nullopt;
   }
   auto out = it->second;
   expire_reachability(out, std::chrono::steady_clock::now());
   return out;
}

std::vector<peer_store::record> peer_store::snapshot() const {
   auto lock = std::scoped_lock{impl_->mutex};
   auto out = std::vector<record>{};
   out.reserve(impl_->records.size());
   const auto now = std::chrono::steady_clock::now();
   for (const auto& [_, record] : impl_->records) {
      auto copy = record;
      expire_reachability(copy, now);
      out.push_back(std::move(copy));
   }
   return out;
}

} // namespace fcl::p2p
