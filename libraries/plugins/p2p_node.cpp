module;

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <fcl/exception/macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module fcl.plugins.p2p_node;

import fcl.api;
import fcl.app.plugin;
import fcl.app.plugin_context;
import fcl.asio.runtime;
import fcl.config.component;
import fcl.config.decode;
import fcl.exception.exception;
import fcl.p2p;

namespace fcl::plugins {
namespace {

enum class outbox_mode {
   memory,
   external_optional,
   external_required,
};

struct parsed_config {
   outbox_mode mode = outbox_mode::memory;
   p2p_node::send_options defaults{};
   std::size_t queue_limit = 4096;
   std::size_t worker_batch = 64;
   bool relay_client_enabled = true;
   bool relay_server_enabled = false;
   bool relay_public_allowed = false;
   bool retry_jitter = true;
   std::chrono::milliseconds relay_reservation_ttl{60'000};
   std::size_t relay_max_candidates = 4;
   std::chrono::milliseconds peer_exchange_interval{30'000};
   std::chrono::milliseconds reachability_interval{60'000};
};

[[nodiscard]] fcl::p2p::peer_id default_test_peer() {
   return fcl::p2p::peer_id{.value = "0000000000000000000000000000000000000000000000000000000000000001"};
}

[[nodiscard]] bool contains_protocol(const std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::protocol_handler>>& routes,
                                     const fcl::p2p::protocol_id& protocol) {
   return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
      return route.first == protocol;
   });
}

[[nodiscard]] std::vector<fcl::quic::endpoint> parse_endpoint_list(const std::vector<std::string>& values) {
   auto out = std::vector<fcl::quic::endpoint>{};
   out.reserve(values.size());
   for (const auto& value : values) {
      out.push_back(fcl::quic::parse_endpoint(value));
   }
   return out;
}

[[nodiscard]] p2p_node::config decode_config(const fcl::config::component_view& view) {
   auto decoded = fcl::config::decode<p2p_node::config>(view.source(), view.section());
   if (!decoded.ok()) {
      auto message = std::string{"invalid P2P node config"};
      if (!decoded.diagnostics.entries.empty()) {
         const auto& first = decoded.diagnostics.entries.front();
         message += ": " + first.path + " " + first.code + " " + first.message;
      }
      throw std::invalid_argument{std::move(message)};
   }
   return std::move(decoded.value);
}

[[nodiscard]] outbox_mode parse_outbox_mode(const std::string& value) {
   if (value == "memory") {
      return outbox_mode::memory;
   }
   if (value == "external-optional") {
      return outbox_mode::external_optional;
   }
   if (value == "external-required") {
      return outbox_mode::external_required;
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy, "invalid P2P outbox mode",
                       fcl::exception::ctx("mode", value));
}

[[nodiscard]] p2p_node::delivery_reliability parse_reliability(const std::string& value) {
   if (value == "best-effort") {
      return p2p_node::delivery_reliability::best_effort;
   }
   if (value == "bounded-retry") {
      return p2p_node::delivery_reliability::bounded_retry;
   }
   if (value == "durable-retry") {
      return p2p_node::delivery_reliability::durable_retry;
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy, "invalid P2P delivery reliability",
                       fcl::exception::ctx("reliability", value));
}

[[nodiscard]] p2p_node::path_policy parse_path_policy(const std::string& value) {
   if (value == "direct-only") {
      return p2p_node::path_policy::direct_only;
   }
   if (value == "direct-preferred") {
      return p2p_node::path_policy::direct_preferred;
   }
   if (value == "relay-only") {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::relay_policy_denied,
                          "P2P relay-only delivery is not exposed until fcl.p2p supports no-direct open policy");
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy, "invalid P2P delivery path policy",
                       fcl::exception::ctx("policy", value));
}

void validate_relay_trust(const std::string& value) {
   if (value == "known-only" || value == "public-allowed") {
      return;
   }
   FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy, "invalid P2P relay trust policy",
                       fcl::exception::ctx("trust", value));
}

[[nodiscard]] std::chrono::milliseconds to_ms(std::uint64_t value) {
   return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(value)};
}

[[nodiscard]] parsed_config parse_policy(const p2p_node::config& config) {
   validate_relay_trust(config.relay_trust);
   auto out = parsed_config{
      .mode = parse_outbox_mode(config.delivery_outbox_mode),
      .defaults =
         p2p_node::send_options{
            .reliability = parse_reliability(config.retry_reliability),
            .path = parse_path_policy(config.path_policy),
            .deadline = to_ms(config.retry_deadline_ms),
            .initial_backoff = to_ms(config.retry_initial_backoff_ms),
            .max_backoff = to_ms(config.retry_max_backoff_ms),
            .max_attempts = static_cast<std::uint32_t>(config.retry_max_attempts),
            .allow_public_relay = config.relay_public_allowed || config.relay_trust == "public-allowed",
            .jitter = config.retry_jitter,
         },
      .queue_limit = static_cast<std::size_t>(config.delivery_queue_limit),
      .worker_batch = static_cast<std::size_t>(config.delivery_worker_batch),
      .relay_client_enabled = config.relay_client_enabled,
      .relay_server_enabled = config.relay_server_enabled,
      .relay_public_allowed = config.relay_public_allowed || config.relay_trust == "public-allowed",
      .retry_jitter = config.retry_jitter,
      .relay_reservation_ttl = to_ms(config.relay_reservation_ttl_ms),
      .relay_max_candidates = static_cast<std::size_t>(config.relay_max_candidates),
      .peer_exchange_interval = to_ms(config.maintenance_peer_exchange_interval_ms),
      .reachability_interval = to_ms(config.maintenance_reachability_interval_ms),
   };
   if (out.defaults.initial_backoff > out.defaults.max_backoff) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy,
                          "P2P retry initial backoff must not exceed max backoff");
   }
   return out;
}

[[nodiscard]] fcl::api::error_identity identity_for(const std::exception& error) {
   if (const auto* fcl_error = dynamic_cast<const fcl::exception::base*>(&error)) {
      const auto& code = fcl_error->code();
      if (code) {
         return fcl::api::error_identity{
            .category = code.category().name(),
            .code = static_cast<std::uint32_t>(code.value()),
         };
      }
   }
   if (const auto* p2p_error = dynamic_cast<const fcl::p2p::p2p_error*>(&error)) {
      return fcl::api::error_identity{
         .category = "fcl.p2p",
         .code = static_cast<std::uint32_t>(p2p_error->kind()) + 1,
      };
   }
   return fcl::api::error_identity{};
}

[[nodiscard]] bool is_retryable(const std::exception& error) {
   if (const auto* p2p_error = dynamic_cast<const fcl::p2p::p2p_error*>(&error)) {
      switch (p2p_error->kind()) {
      case fcl::p2p::error_kind::peer_not_found:
      case fcl::p2p::error_kind::relay_not_available:
      case fcl::p2p::error_kind::relay_rejected:
      case fcl::p2p::error_kind::backpressure_rejected:
      case fcl::p2p::error_kind::timeout:
      case fcl::p2p::error_kind::closed:
         return true;
      default:
         return false;
      }
   }
   const auto* fcl_error = dynamic_cast<const fcl::exception::base*>(&error);
   if (fcl_error == nullptr || !fcl_error->code()) {
      return false;
   }
   if (fcl_error->code().category().name() != std::string{"fcl.p2p"}) {
      return false;
   }
   switch (static_cast<fcl::p2p::exceptions::code>(fcl_error->code().value())) {
   case fcl::p2p::exceptions::code::peer_not_found:
   case fcl::p2p::exceptions::code::relay_not_available:
   case fcl::p2p::exceptions::code::relay_rejected:
   case fcl::p2p::exceptions::code::backpressure_rejected:
   case fcl::p2p::exceptions::code::timeout:
   case fcl::p2p::exceptions::code::closed:
      return true;
   default:
      return false;
   }
}

[[nodiscard]] p2p_node::delivery_snapshot snapshot_for(const p2p_node::outbox_record& record) {
   return p2p_node::delivery_snapshot{
      .id = record.id,
      .peer = record.peer,
      .protocol = record.message.protocol(),
      .state = record.state,
      .attempts = record.attempts,
      .error = record.last_error,
      .error_identity = record.last_error_identity,
   };
}

[[nodiscard]] p2p_node::delivery_result result_for(const p2p_node::outbox_record& record,
                                                   p2p_node::delivery_state state) {
   return p2p_node::delivery_result{
      .id = record.id,
      .peer = record.peer,
      .protocol = record.message.protocol(),
      .state = state,
      .attempts = record.attempts,
      .error = record.last_error,
      .error_identity = record.last_error_identity,
   };
}

[[nodiscard]] p2p_node::delivery_result result_for(const p2p_node::delivery_snapshot& snapshot) {
   return p2p_node::delivery_result{
      .id = snapshot.id,
      .peer = snapshot.peer,
      .protocol = snapshot.protocol,
      .state = snapshot.state,
      .attempts = snapshot.attempts,
      .error = snapshot.error,
      .error_identity = snapshot.error_identity,
   };
}

[[nodiscard]] bool terminal_state(p2p_node::delivery_state state) noexcept {
   return state == p2p_node::delivery_state::delivered || state == p2p_node::delivery_state::failed ||
          state == p2p_node::delivery_state::expired || state == p2p_node::delivery_state::cancelled;
}

[[nodiscard]] bool is_default_options(const p2p_node::send_options& value) {
   const auto defaults = p2p_node::send_options{};
   return value.reliability == defaults.reliability && value.path == defaults.path &&
          value.deadline == defaults.deadline && value.initial_backoff == defaults.initial_backoff &&
          value.max_backoff == defaults.max_backoff && value.max_attempts == defaults.max_attempts &&
          value.priority == defaults.priority && value.allow_public_relay == defaults.allow_public_relay &&
          value.jitter == defaults.jitter;
}

class in_memory_outbox_store final : public p2p_node::outbox_store {
 public:
   explicit in_memory_outbox_store(std::size_t limit) : limit_{limit} {}

   boost::asio::awaitable<p2p_node::delivery_id> enqueue(p2p_node::outbox_record record) override {
      const auto lock = std::scoped_lock{mutex_};
      if (records_.size() >= limit_) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::delivery_queue_full, "P2P delivery outbox is full");
      }
      if (record.id.value == 0) {
         record.id.value = next_id_++;
      }
      record.state = p2p_node::delivery_state::queued;
      const auto id = record.id;
      records_.insert_or_assign(id.value, std::move(record));
      co_return id;
   }

   boost::asio::awaitable<std::vector<p2p_node::outbox_record>>
   claim_due(std::size_t limit, std::chrono::steady_clock::time_point now) override {
      auto out = std::vector<p2p_node::outbox_record>{};
      const auto lock = std::scoped_lock{mutex_};
      for (auto& [ignored, record] : records_) {
         static_cast<void>(ignored);
         if (out.size() >= limit) {
            break;
         }
         if (record.state == p2p_node::delivery_state::queued && record.next_attempt_at <= now) {
            record.state = p2p_node::delivery_state::in_flight;
            out.push_back(record);
         }
      }
      co_return out;
   }

   boost::asio::awaitable<void> mark_attempt(p2p_node::outbox_record record) override {
      const auto lock = std::scoped_lock{mutex_};
      record.state = p2p_node::delivery_state::in_flight;
      records_.insert_or_assign(record.id.value, std::move(record));
      co_return;
   }

   boost::asio::awaitable<void> release(p2p_node::outbox_record record) override {
      const auto lock = std::scoped_lock{mutex_};
      if (record.state != p2p_node::delivery_state::delivered && record.state != p2p_node::delivery_state::failed &&
          record.state != p2p_node::delivery_state::expired && record.state != p2p_node::delivery_state::cancelled) {
         record.state = p2p_node::delivery_state::queued;
      }
      records_.insert_or_assign(record.id.value, std::move(record));
      co_return;
   }

   boost::asio::awaitable<void> mark_delivered(p2p_node::delivery_result result) override {
      const auto lock = std::scoped_lock{mutex_};
      if (auto found = records_.find(result.id.value); found != records_.end()) {
         found->second.state = p2p_node::delivery_state::delivered;
         found->second.attempts = result.attempts;
         found->second.last_error.clear();
         found->second.last_error_identity = {};
      }
      co_return;
   }

   boost::asio::awaitable<void> mark_failed(p2p_node::delivery_result result) override {
      const auto lock = std::scoped_lock{mutex_};
      if (auto found = records_.find(result.id.value); found != records_.end()) {
         found->second.state = result.state;
         found->second.attempts = result.attempts;
         found->second.last_error = result.error;
         found->second.last_error_identity = result.error_identity;
      }
      co_return;
   }

   boost::asio::awaitable<std::optional<p2p_node::delivery_snapshot>> get(p2p_node::delivery_id id) const override {
      const auto lock = std::scoped_lock{mutex_};
      const auto found = records_.find(id.value);
      if (found == records_.end()) {
         co_return std::nullopt;
      }
      co_return snapshot_for(found->second);
   }

   boost::asio::awaitable<void> cancel(p2p_node::delivery_id id) override {
      const auto lock = std::scoped_lock{mutex_};
      if (auto found = records_.find(id.value); found != records_.end()) {
         found->second.state = p2p_node::delivery_state::cancelled;
      }
      co_return;
   }

   boost::asio::awaitable<std::optional<std::chrono::steady_clock::time_point>> next_due() const override {
      auto result = std::optional<std::chrono::steady_clock::time_point>{};
      const auto lock = std::scoped_lock{mutex_};
      for (const auto& [ignored, record] : records_) {
         static_cast<void>(ignored);
         if (record.state != p2p_node::delivery_state::queued) {
            continue;
         }
         if (!result || record.next_attempt_at < *result) {
            result = record.next_attempt_at;
         }
      }
      co_return result;
   }

 private:
   std::size_t limit_ = 0;
   mutable std::mutex mutex_;
   std::uint64_t next_id_ = 1;
   std::map<std::uint64_t, p2p_node::outbox_record> records_;
};

} // namespace

struct p2p_node::impl : public std::enable_shared_from_this<p2p_node::impl> {
   fcl::p2p::node_options options{
      .explicit_peer_id = default_test_peer(),
      .allow_insecure_test_mode = false,
   };
   fcl::api::codec_id api_codec{.value = "fcl.raw"};
   parsed_config policy{};
   std::size_t max_inflight_per_peer = 64;
   std::vector<fcl::quic::endpoint> listen;
   std::vector<fcl::quic::endpoint> bootstrap;
   std::vector<std::pair<fcl::p2p::protocol_id, fcl::p2p::protocol_handler>> routes;
   fcl::p2p::node* raw = nullptr;
   std::unique_ptr<fcl::p2p::node> node;
   fcl::asio::runtime* runtime = nullptr;
   std::shared_ptr<outbox_store> outbox;
   std::shared_ptr<in_memory_outbox_store> memory_outbox;
   std::unique_ptr<boost::asio::steady_timer> delivery_timer;
   std::unique_ptr<boost::asio::steady_timer> maintenance_timer;
   bool delivery_drain_active = false;
   bool maintenance_active = false;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] fcl::p2p::node& require_node() {
      if (!node) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P node plugin is not initialized");
      }
      return *node;
   }

   void add_route(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) {
      if (started) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "P2P routes must be published before p2p_node startup",
                             fcl::exception::ctx("protocol", protocol.value));
      }
      if (protocol.value.empty() || !handler) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "P2P route is invalid");
      }
      if (contains_protocol(routes, protocol)) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::route_conflict, "duplicate P2P route",
                             fcl::exception::ctx("protocol", protocol.value));
      }
      routes.emplace_back(std::move(protocol), std::move(handler));
   }

   [[nodiscard]] fcl::p2p::open_options open_options_for(const send_options& options_value) const {
      if (options_value.path == path_policy::relay_only) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::relay_policy_denied,
                             "P2P relay-only delivery is not exposed until fcl.p2p supports no-direct open policy");
      }
      if (options_value.path != path_policy::direct_only && !options_value.allow_public_relay &&
          policy.relay_public_allowed) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::relay_policy_denied,
                             "P2P public relay policy must be opted into by the message sender");
      }
      auto out = fcl::p2p::open_options{
         .allow_relay = policy.relay_client_enabled && options_value.path != path_policy::direct_only,
         .timeout = options_value.deadline,
         .direct_attempt_timeout = std::min(std::chrono::milliseconds{2'000}, options_value.deadline),
         .relay_attempt_timeout = std::min(std::chrono::milliseconds{5'000}, options_value.deadline),
         .max_relay_candidates = policy.relay_max_candidates,
         .allow_hole_punch = policy.relay_client_enabled && options_value.path == path_policy::direct_preferred,
      };
      return out;
   }

   [[nodiscard]] send_options effective_options(send_options options_value) const {
      if (is_default_options(options_value)) {
         options_value = policy.defaults;
      }
      return options_value;
   }

   [[nodiscard]] outbox_record make_record(fcl::p2p::peer_id peer, fcl::p2p::message message, send_options options_value) {
      options_value = effective_options(options_value);
      if (options_value.max_attempts == 0 || options_value.deadline.count() <= 0 ||
          options_value.initial_backoff.count() <= 0 || options_value.max_backoff.count() <= 0 ||
          options_value.initial_backoff > options_value.max_backoff) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::invalid_delivery_policy, "invalid P2P delivery options");
      }
      const auto now = std::chrono::steady_clock::now();
      return outbox_record{
         .peer = std::move(peer),
         .message = std::move(message),
         .options = options_value,
         .created_at = now,
         .deadline_at = now + options_value.deadline,
         .next_attempt_at = now,
      };
   }

   [[nodiscard]] bool terminal(delivery_state state) const noexcept {
      return terminal_state(state);
   }

   boost::asio::awaitable<delivery_result> attempt_once(outbox_record& record) {
      ++record.attempts;
      try {
         auto stream = co_await require_node().async_open_protocol_stream(record.peer, record.message.protocol(),
                                                                          open_options_for(record.options));
         co_await stream.async_write_frame(
            std::span<const std::uint8_t>{record.message.data().data(), record.message.data().size()});
         record.state = delivery_state::delivered;
         record.last_error.clear();
         record.last_error_identity = {};
         co_return result_for(record, delivery_state::delivered);
      } catch (const std::exception& error) {
         record.last_error = error.what();
         record.last_error_identity = identity_for(error);
         if (!is_retryable(error)) {
            record.state = delivery_state::failed;
            co_return result_for(record, delivery_state::failed);
         }
      }
      const auto now = std::chrono::steady_clock::now();
      if (now >= record.deadline_at) {
         record.state = delivery_state::expired;
         co_return result_for(record, delivery_state::expired);
      }
      if (record.attempts >= record.options.max_attempts ||
          record.options.reliability == delivery_reliability::best_effort) {
         record.state = delivery_state::failed;
         co_return result_for(record, delivery_state::failed);
      }
      const auto scale = std::uint64_t{1} << std::min<std::uint32_t>(record.attempts - 1, 16);
      auto delay = record.options.initial_backoff * static_cast<int>(scale);
      if (delay > record.options.max_backoff) {
         delay = record.options.max_backoff;
      }
      if (record.options.jitter && delay.count() > 1) {
         const auto spread = delay / 4;
         if (spread.count() > 0) {
            const auto seed = record.id.value + static_cast<std::uint64_t>(record.attempts) * 1103515245ULL;
            delay += std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(seed %
                                                                                           (static_cast<std::uint64_t>(
                                                                                               spread.count()) +
                                                                                            1ULL))};
            if (delay > record.options.max_backoff) {
               delay = record.options.max_backoff;
            }
         }
      }
      record.next_attempt_at = std::min(now + delay, record.deadline_at);
      record.state = delivery_state::queued;
      co_return result_for(record, delivery_state::queued);
   }

   boost::asio::awaitable<void> drain_due_deliveries() {
      if (delivery_drain_active || stopping || !outbox) {
         co_return;
      }
      delivery_drain_active = true;
      try {
         auto due = co_await outbox->claim_due(policy.worker_batch, std::chrono::steady_clock::now());
         for (auto& record : due) {
            if (stopping) {
               co_await outbox->release(std::move(record));
               continue;
            }
            auto result = co_await attempt_once(record);
            if (record.state == delivery_state::delivered) {
               co_await outbox->mark_delivered(result);
            } else if (terminal(record.state)) {
               co_await outbox->mark_failed(result);
            } else {
               co_await outbox->release(std::move(record));
            }
         }
      } catch (...) {
         fcl::exception::capture_and_log("P2P delivery drain failed");
      }
      delivery_drain_active = false;
      schedule_delivery_drain();
   }

   void schedule_delivery_drain() {
      if (stopping || !delivery_timer || !outbox) {
         return;
      }
      boost::asio::co_spawn(
         runtime->context(),
         [self = shared_from_this()]() -> boost::asio::awaitable<void> {
            auto next = co_await self->outbox->next_due();
            if (!next || self->stopping || !self->delivery_timer) {
               co_return;
            }
            const auto now = std::chrono::steady_clock::now();
            self->delivery_timer->expires_at(*next <= now ? now : *next);
            auto error = boost::system::error_code{};
            co_await self->delivery_timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            if (!error && !self->stopping) {
               co_await self->drain_due_deliveries();
            }
         },
         boost::asio::detached);
   }

   boost::asio::awaitable<void> run_maintenance_once() {
      if (!node || stopping) {
         co_return;
      }
      const auto snapshot = node->peers().snapshot();
      auto relay_checked = std::size_t{0};
      for (const auto& peer : snapshot) {
         if (stopping) {
            co_return;
         }
         try {
            co_await node->async_request_peer_exchange(peer.peer);
         } catch (...) {
            fcl::exception::capture_and_log("P2P peer exchange maintenance failed");
         }
         try {
            (void)co_await node->async_probe_reachability(peer.peer);
         } catch (...) {
            fcl::exception::capture_and_log("P2P reachability maintenance failed");
         }
         if (policy.relay_client_enabled && relay_checked < policy.relay_max_candidates &&
             peer.capabilities.has(fcl::p2p::capabilities::relay) &&
             peer.capabilities.has(fcl::p2p::capabilities::relay_reservation)) {
            ++relay_checked;
            try {
               (void)co_await node->async_reserve_relay(
                  peer.peer, fcl::p2p::relay_reservation_options{.ttl = policy.relay_reservation_ttl});
            } catch (...) {
               fcl::exception::capture_and_log("P2P relay reservation maintenance failed");
            }
         }
      }
   }

   void schedule_maintenance() {
      if (stopping || !maintenance_timer || !node || maintenance_active) {
         return;
      }
      maintenance_active = true;
      boost::asio::co_spawn(
         runtime->context(),
         [self = shared_from_this()]() -> boost::asio::awaitable<void> {
            const auto interval = std::min(self->policy.peer_exchange_interval, self->policy.reachability_interval);
            self->maintenance_timer->expires_after(interval);
            auto error = boost::system::error_code{};
            co_await self->maintenance_timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            if (!error && !self->stopping) {
               co_await self->run_maintenance_once();
            }
            self->maintenance_active = false;
            self->schedule_maintenance();
         },
         boost::asio::detached);
   }
};

class p2p_node::delivery::impl final {
 public:
   impl(delivery_id id, std::shared_ptr<outbox_store> outbox, fcl::asio::runtime* runtime)
       : id_{id}, outbox_{std::move(outbox)}, runtime_{runtime} {}

   [[nodiscard]] delivery_id id() const noexcept {
      return id_;
   }

   boost::asio::awaitable<std::optional<delivery_snapshot>> snapshot() const {
      if (!outbox_) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::outbox_unavailable, "P2P delivery handle has no outbox");
      }
      co_return co_await outbox_->get(id_);
   }

   boost::asio::awaitable<delivery_result> result() const {
      if (!runtime_) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P delivery handle has no runtime");
      }
      for (;;) {
         auto current = co_await snapshot();
         if (!current) {
            FCL_THROW_EXCEPTION(p2p_node::exceptions::outbox_unavailable, "P2P delivery record is not available");
         }
         if (terminal_state(current->state)) {
            co_return result_for(*current);
         }
         auto timer = boost::asio::steady_timer{runtime_->context()};
         timer.expires_after(std::chrono::milliseconds{10});
         auto error = boost::system::error_code{};
         co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
         static_cast<void>(error);
      }
   }

   boost::asio::awaitable<void> cancel() {
      if (!outbox_) {
         FCL_THROW_EXCEPTION(p2p_node::exceptions::outbox_unavailable, "P2P delivery handle has no outbox");
      }
      co_await outbox_->cancel(id_);
   }

 private:
   delivery_id id_;
   std::shared_ptr<outbox_store> outbox_;
   fcl::asio::runtime* runtime_ = nullptr;
};

p2p_node::delivery::delivery() = default;
p2p_node::delivery::~delivery() = default;

p2p_node::delivery::delivery(std::shared_ptr<impl> impl) : impl_{std::move(impl)} {}

p2p_node::delivery_id p2p_node::delivery::id() const noexcept {
   if (!impl_) {
      return {};
   }
   return impl_->id();
}

boost::asio::awaitable<std::optional<p2p_node::delivery_snapshot>> p2p_node::delivery::snapshot() const {
   if (!impl_) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P delivery handle is empty");
   }
   co_return co_await impl_->snapshot();
}

boost::asio::awaitable<p2p_node::delivery_result> p2p_node::delivery::result() const {
   if (!impl_) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P delivery handle is empty");
   }
   co_return co_await impl_->result();
}

boost::asio::awaitable<void> p2p_node::delivery::cancel() {
   if (!impl_) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::plugin_not_initialized, "P2P delivery handle is empty");
   }
   co_await impl_->cancel();
}

class p2p_node::api::impl final : public p2p_node::api {
 public:
   explicit impl(std::shared_ptr<p2p_node::impl> impl) : impl_{std::move(impl)} {}

   fcl::p2p::peer_id local_peer() const override {
      return impl_->require_node().local_peer();
   }

   std::optional<fcl::quic::endpoint> local_endpoint() const override {
      return impl_->require_node().local_endpoint();
   }

   fcl::p2p::node_metrics metrics() const override {
      return impl_->require_node().metrics();
   }

   std::vector<fcl::p2p::peer_record> peers() const override {
      return impl_->require_node().peers().snapshot();
   }

   void publish_api(fcl::api::binding_plan plan, fcl::p2p::protocol_id protocol) override {
      auto binding = fcl::p2p::api()
                        .use(std::move(plan))
                        .protocol_id(protocol)
                        .codec(impl_->api_codec)
                        .max_inflight_per_peer(impl_->max_inflight_per_peer)
                        .build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   void publish_protocol(fcl::p2p::protocol_id protocol, fcl::p2p::protocol_handler handler) override {
      auto binding = fcl::p2p::route().protocol_id(std::move(protocol)).handler(std::move(handler)).build();
      impl_->add_route(binding.protocol(), binding.handler());
   }

   boost::asio::awaitable<p2p_node::delivery> send_async(fcl::p2p::peer_id peer, fcl::p2p::message message,
                                                         send_options options) override {
      auto record = impl_->make_record(std::move(peer), std::move(message), options);
      auto id = co_await impl_->outbox->enqueue(std::move(record));
      impl_->schedule_delivery_drain();
      co_return p2p_node::delivery{
         std::make_shared<p2p_node::delivery::impl>(id, impl_->outbox, impl_->runtime)};
   }

   boost::asio::awaitable<std::vector<p2p_node::delivery>> broadcast_async(fcl::p2p::message message,
                                                                           broadcast_options options) override {
      auto peers = std::move(options.peers);
      if (peers.empty()) {
         for (const auto& peer : impl_->require_node().peers().snapshot()) {
            peers.push_back(peer.peer);
         }
      }

      auto out = std::vector<p2p_node::delivery>{};
      out.reserve(peers.size());
      for (auto& peer : peers) {
         out.push_back(co_await send_async(std::move(peer), message, options.send));
      }
      co_return out;
   }

   [[nodiscard]] auto delivery(delivery_id id) const -> p2p_node::delivery override {
      return p2p_node::delivery{std::make_shared<p2p_node::delivery::impl>(id, impl_->outbox, impl_->runtime)};
   }

   boost::asio::awaitable<void> cancel(delivery_id id) override {
      co_await impl_->outbox->cancel(id);
   }

 private:
   std::shared_ptr<p2p_node::impl> impl_;
};

fcl::api::descriptor p2p_node::api::describe() {
   return fcl::api::contract<p2p_node::api>({.id = {"fcl.plugins.p2p_node"}, .version = {.major = 1, .revision = 0}})
      .build();
}

fcl::api::descriptor p2p_node::outbox_store::describe() {
   return fcl::api::contract<p2p_node::outbox_store>(
             {.id = {"fcl.plugins.p2p_node.outbox"}, .version = {.major = 1, .revision = 0}})
      .build();
}

boost::asio::awaitable<p2p_node::delivery> p2p_node::api::send_async(fcl::p2p::peer_id peer,
                                                                     fcl::p2p::message message) {
   co_return co_await send_async(std::move(peer), std::move(message), send_options{});
}

boost::asio::awaitable<p2p_node::delivery_result> p2p_node::api::send(fcl::p2p::peer_id peer,
                                                                      fcl::p2p::message message) {
   co_return co_await send(std::move(peer), std::move(message), send_options{});
}

boost::asio::awaitable<p2p_node::delivery_result> p2p_node::api::send(fcl::p2p::peer_id peer,
                                                                      fcl::p2p::message message,
                                                                      send_options options) {
   auto delivery_handle = co_await send_async(std::move(peer), std::move(message), options);
   co_return co_await delivery_handle.result();
}

boost::asio::awaitable<std::vector<p2p_node::delivery>> p2p_node::api::broadcast_async(fcl::p2p::message message) {
   co_return co_await broadcast_async(std::move(message), broadcast_options{});
}

boost::asio::awaitable<std::vector<p2p_node::delivery_result>> p2p_node::api::broadcast(fcl::p2p::message message) {
   co_return co_await broadcast(std::move(message), broadcast_options{});
}

boost::asio::awaitable<std::vector<p2p_node::delivery_result>>
p2p_node::api::broadcast(fcl::p2p::message message, broadcast_options options) {
   auto deliveries = co_await broadcast_async(std::move(message), std::move(options));
   auto out = std::vector<delivery_result>{};
   out.reserve(deliveries.size());
   for (auto& delivery_handle : deliveries) {
      out.push_back(co_await delivery_handle.result());
   }
   co_return out;
}

p2p_node::p2p_node() : impl_{std::make_shared<impl>()} {}
p2p_node::~p2p_node() = default;

fcl::app::plugin_id p2p_node::id() const {
   return fcl::app::plugin_id{.value = "fcl.p2p_node"};
}

std::string p2p_node::version() const {
   return "1.0.0";
}

std::optional<fcl::config::component_descriptor> p2p_node::describe_config() const {
   return fcl::config::describe_component<p2p_node::config>("p2p");
}

boost::asio::awaitable<void> p2p_node::configure(fcl::config::component_view view) {
   const auto config = decode_config(view);
   impl_->policy = parse_policy(config);
   impl_->api_codec = fcl::api::codec_id{.value = config.api_codec};
   impl_->listen = parse_endpoint_list(config.listen);
   impl_->bootstrap = parse_endpoint_list(config.bootstrap);
   impl_->options.advertised_endpoints = parse_endpoint_list(config.advertised_endpoints);
   impl_->options.certificate_pem = config.certificate_pem;
   impl_->options.private_key_pem = config.private_key_pem;
   impl_->options.capabilities = fcl::p2p::capability_set{
      .bits = fcl::p2p::capabilities::direct_quic | fcl::p2p::capabilities::peer_exchange,
   };
   if (impl_->policy.relay_client_enabled) {
      impl_->options.capabilities.add(fcl::p2p::capabilities::hole_punching);
   }
   if (impl_->policy.relay_server_enabled) {
      impl_->options.capabilities.add(fcl::p2p::capabilities::relay);
      impl_->options.capabilities.add(fcl::p2p::capabilities::relay_reservation);
   }
   impl_->options.limits.relay.reservation_ttl = impl_->policy.relay_reservation_ttl;
   const auto& peer_id = config.peer_id;
   impl_->options.explicit_peer_id = peer_id.empty() ? default_test_peer() : fcl::p2p::peer_id{.value = peer_id};
   impl_->max_inflight_per_peer = static_cast<std::size_t>(config.max_inflight_per_peer);
   impl_->options.limits.max_sessions = static_cast<std::size_t>(config.max_sessions);
   impl_->options.limits.max_protocol_handlers = static_cast<std::size_t>(config.max_protocol_handlers);
   impl_->options.allow_insecure_test_mode = config.allow_insecure_test_mode;
   co_return;
}

boost::asio::awaitable<void> p2p_node::provide(fcl::api::provider& provider) {
   provider.install<p2p_node::api>(p2p_node::api::describe(), std::make_shared<p2p_node::api::impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> p2p_node::initialize(fcl::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->stopping = false;
   impl_->memory_outbox = std::make_shared<in_memory_outbox_store>(impl_->policy.queue_limit);
   if (auto external = context.apis().try_get<p2p_node::outbox_store>(
          {.id = {"fcl.plugins.p2p_node.outbox"}, .major = 1, .min_revision = 0});
       external) {
      impl_->outbox = external.shared();
   } else if (impl_->policy.mode == outbox_mode::external_required) {
      FCL_THROW_EXCEPTION(p2p_node::exceptions::outbox_required, "P2P node requires external outbox store");
   } else {
      impl_->outbox = impl_->memory_outbox;
   }
   impl_->delivery_timer = std::make_unique<boost::asio::steady_timer>(impl_->runtime->context());
   impl_->maintenance_timer = std::make_unique<boost::asio::steady_timer>(impl_->runtime->context());
   impl_->node = std::make_unique<fcl::p2p::node>(context.scheduler().runtime_context(), impl_->options);
   impl_->raw = impl_->node.get();
   co_return;
}

boost::asio::awaitable<void> p2p_node::startup() {
   auto& node = impl_->require_node();
   for (auto& route : impl_->routes) {
      node.register_protocol_handler(route.first, route.second);
   }
   for (const auto& endpoint : impl_->listen) {
      co_await node.async_listen(endpoint);
   }
   for (const auto& endpoint : impl_->bootstrap) {
      try {
         (void)co_await node.async_connect(endpoint);
      } catch (...) {
         fcl::exception::capture_and_log("P2P bootstrap connect failed");
      }
   }
   impl_->started = true;
   impl_->schedule_delivery_drain();
   impl_->schedule_maintenance();
}

void p2p_node::request_stop() noexcept {
   impl_->stopping = true;
   if (impl_->delivery_timer) {
      impl_->delivery_timer->cancel();
   }
   if (impl_->maintenance_timer) {
      impl_->maintenance_timer->cancel();
   }
   if (impl_->raw) {
      impl_->raw->stop();
   }
}

boost::asio::awaitable<void> p2p_node::shutdown() {
   request_stop();
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->delivery_timer.reset();
   impl_->maintenance_timer.reset();
   impl_->started = false;
}

fcl::app::plugin_descriptor p2p_node::descriptor() {
   return fcl::app::plugin_descriptor{
      .id = fcl::app::plugin_id{.value = "fcl.p2p_node"},
      .factory = [] {
         return std::make_unique<p2p_node>();
      },
   };
}

} // namespace fcl::plugins
