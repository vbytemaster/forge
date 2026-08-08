#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "p2p_identity.hpp"

import forge.api.core.binding;
import forge.api.core.exceptions;
import forge.api.core.registry;
import forge.api.core.types;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.api.transport.options;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.asio.blocking;
import forge.asio.exceptions;
import forge.asio.runtime;
import forge.chain.api.admin;
import forge.chain.api.authenticated_audit_verifier;
import forge.chain.api.block;
import forge.chain.api.finality;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.api.raw_client;
import forge.chain.api.state;
import forge.chain.api.submission;
import forge.chain.api.submission_client;
import forge.chain.api.table_key;
import forge.chain.api.transaction;
import forge.chain.api.verified_client;
import forge.chain.protocol.admin;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction_query;
import forge.config.core.document;
import forge.config.core.value;
import forge.crypto.digest.sha256;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.router;
import forge.net.http.server;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.plugin;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.plugin;
import forge.plugins.p2p.resolver.types;

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

class accepting_finality final : public chain_api::finality_verifier {
 public:
   void verify(const protocol::state_anchor&, const protocol::proof_blob&) override {
      ++calls;
   }

   void verify_ancestry(const protocol::state_anchor&, std::span<const protocol::state_anchor>,
                        const protocol::proof_blob&) override {
      ++calls;
   }

   std::size_t calls = 0;
};

constexpr auto chain_api_protocol = std::string_view{"/spine/chain/api/1"};
constexpr auto chain_api_max_frame_size = std::uint32_t{64U * 1024U};

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

protocol::digest hash(std::string_view value) {
   return forge::crypto::digest::sha256::hash(std::string{value});
}

protocol::audit_class audit_class_for(std::string_view api, std::string_view method) {
   using enum protocol::audit_class;
   if (api == "forge.chain.api.info") {
      return finality;
   }
   if (api == "forge.chain.api.block") {
      return unsupported;
   }
   if (api == "forge.chain.api.state") {
      return unsupported;
   }
   if (api == "forge.chain.api.transaction") {
      return unsupported;
   }
   return none;
}

template <typename Interface> void append_capabilities(protocol::capabilities& result) {
   const auto descriptor = Interface::describe();
   for (const auto& method : descriptor.methods) {
      result.methods.push_back(protocol::method_capability{
          .api = descriptor.id.value,
          .method = method.name,
          .audit = audit_class_for(descriptor.id.value, method.name),
          .enabled = true,
          .http = true,
          .p2p = true,
      });
   }
}

protocol::service_limits package_limits() {
   return {
       .max_page_size = 256,
       .max_state_batch_size = 32,
       .max_transaction_batch_size = 16,
       .max_container_elements = 1'024,
       .max_transaction_status_candidates = 512,
       .max_request_bytes = chain_api_max_frame_size,
       .max_response_bytes = chain_api_max_frame_size,
       .max_proof_bytes = 1U << 20U,
       .max_await_ms = 300'000,
       .state_retention_blocks = 512,
   };
}

protocol::info_response make_info_response() {
   const auto chain = hash("chain-api-e2e-chain");
   const auto head = hash("chain-api-e2e-head");
   const auto finalized = hash("chain-api-e2e-finalized");

   auto response = protocol::info_response{};
   response.context = protocol::response_context{
       .chain = chain,
       .head = head,
       .finalized = finalized,
       .anchor =
           protocol::state_anchor{
               .chain = chain,
               .block = finalized,
               .block_num = 40,
               .transaction_root = hash("chain-api-e2e-transactions"),
               .state_root = hash("chain-api-e2e-state"),
               .state_size = 17,
               .change_root = hash("chain-api-e2e-changes"),
               .change_count = 5,
           },
   };
   response.audit = protocol::audit_bundle{
       .finality =
           protocol::proof_blob{
               .scheme = "forge.chain.finality.test",
               .version = 3,
               .payload = {0x01, 0x02, 0x03},
           },
       .state =
           {
               protocol::proof_blob{
                   .scheme = "forge.db.authenticated.point",
                   .version = 2,
                   .payload = {0x10, 0x11, 0x12, 0x13},
               },
               protocol::proof_blob{
                   .scheme = "forge.db.authenticated.range",
                   .version = 4,
                   .payload = {0x20, 0x21, 0x22},
               },
           },
       .transaction =
           protocol::transaction_inclusion_proof{
               .leaf = hash("chain-api-e2e-transaction"),
               .index = 3,
               .leaf_count = 8,
               .path =
                   {
                       protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-left"), .sibling_on_left = true},
                       protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-right"), .sibling_on_left = false},
                   },
           },
   };
   response.chain = chain;
   response.server_version = "1.2.3";
   response.server_version_string = "forge-chain-api-e2e";
   response.server_full_version_string = "forge-chain-api-e2e+transport";
   response.head = head;
   response.head_num = 42;
   response.head_time = protocol::time_point{protocol::microseconds{1'700'000'000'123'456LL}};
   response.finalized = finalized;
   response.finalized_num = 40;
   response.finalized_time = protocol::time_point{protocol::microseconds{1'699'999'999'654'321LL}};
   response.best_candidate = hash("chain-api-e2e-candidate");
   response.best_candidate_num = 43;
   response.earliest_available_block_num = 7;
   response.virtual_block_cpu_limit = 1'000;
   response.virtual_block_net_limit = 2'000;
   response.block_cpu_limit = 900;
   response.block_net_limit = 1'800;
   response.total_cpu_weight = 77;
   response.total_net_weight = 88;
   response.available.archive = true;
   append_capabilities<chain_api::info>(response.available);
   append_capabilities<chain_api::block>(response.available);
   append_capabilities<chain_api::state>(response.available);
   append_capabilities<chain_api::transaction>(response.available);
   append_capabilities<chain_api::submission>(response.available);
   append_capabilities<chain_api::admin>(response.available);
   response.limits = package_limits();
   chain_api::require_response_within_limits(response, response.limits);
   return response;
}

class info_implementation final : public chain_api::info {
 public:
   explicit info_implementation(protocol::info_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::info_response> get(protocol::anchored_request request) override {
      last_audit.store(request.audit, std::memory_order_relaxed);
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};

 private:
   protocol::info_response response_;
};

protocol::block_state_response make_block_state_response(const protocol::info_response& source) {
   auto response = protocol::block_state_response{};
   response.context = source.context;
   response.audit = source.audit;
   response.id = hash("chain-api-e2e-block-state");
   response.num = 40;
   response.state = {0x31, 0x32, 0x33};
   return response;
}

protocol::state_point_response make_state_point_response(const protocol::info_response& source) {
   auto response = protocol::state_point_response{};
   response.context = source.context;
   response.audit = source.audit;
   response.value = protocol::bytes{0x41, 0x42, 0x43};
   return response;
}

protocol::transaction_read_only_response make_transaction_response(const protocol::info_response& source) {
   auto response = protocol::transaction_read_only_response{};
   response.context = source.context;
   response.audit = source.audit;
   response.id = hash("chain-api-e2e-read-only-transaction");
   return response;
}

protocol::producer_status_response make_admin_response() {
   auto response = protocol::producer_status_response{};
   response.paused = true;
   response.scheduled_protocol_features = {hash("chain-api-e2e-protocol-feature")};
   return response;
}

class block_implementation final : public chain_api::block {
 public:
   explicit block_implementation(protocol::block_state_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::block_response> get_block(protocol::block_request) override {
      co_return protocol::block_response{};
   }

   boost::asio::awaitable<protocol::block_header_response> get_header(protocol::block_request) override {
      co_return protocol::block_header_response{};
   }

   boost::asio::awaitable<protocol::block_state_response> get_block_state(protocol::block_request request) override {
      last_audit.store(request.audit, std::memory_order_relaxed);
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   boost::asio::awaitable<protocol::block_range_response> get_canonical_range(protocol::block_range_request) override {
      co_return protocol::block_range_response{};
   }

   boost::asio::awaitable<protocol::protocol_features_response>
   get_activated_protocol_features(protocol::protocol_features_request) override {
      co_return protocol::protocol_features_response{};
   }

   boost::asio::awaitable<protocol::consensus_parameters_response>
   get_consensus_parameters(protocol::anchored_request) override {
      co_return protocol::consensus_parameters_response{};
   }

   boost::asio::awaitable<protocol::producers_response> get_producers(protocol::producers_request) override {
      co_return protocol::producers_response{};
   }

   boost::asio::awaitable<protocol::producer_schedule_response>
   get_producer_schedule(protocol::anchored_request) override {
      co_return protocol::producer_schedule_response{};
   }

   boost::asio::awaitable<protocol::finalizer_info_response> get_finalizer_info(protocol::anchored_request) override {
      co_return protocol::finalizer_info_response{};
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};

 private:
   protocol::block_state_response response_;
};

class state_implementation final : public chain_api::state {
 public:
   explicit state_implementation(protocol::state_point_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::state_point_response> get_point(protocol::state_point_request request) override {
      last_audit.store(request.audit, std::memory_order_relaxed);
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   boost::asio::awaitable<protocol::state_range_response> get_range(protocol::state_range_request) override {
      co_return protocol::state_range_response{};
   }

   boost::asio::awaitable<protocol::state_changes_response> get_changes(protocol::state_changes_request) override {
      co_return protocol::state_changes_response{};
   }

   boost::asio::awaitable<protocol::account_response> get_account(protocol::account_request) override {
      co_return protocol::account_response{};
   }

   boost::asio::awaitable<protocol::code_response> get_code(protocol::code_request) override {
      co_return protocol::code_response{};
   }

   boost::asio::awaitable<protocol::table_rows_response> get_table_rows(protocol::table_rows_request) override {
      co_return protocol::table_rows_response{};
   }

   boost::asio::awaitable<protocol::table_scope_response> get_table_scope(protocol::table_scope_request) override {
      co_return protocol::table_scope_response{};
   }

   boost::asio::awaitable<protocol::currency_balance_response>
   get_currency_balance(protocol::currency_balance_request) override {
      co_return protocol::currency_balance_response{};
   }

   boost::asio::awaitable<protocol::currency_stats_response>
   get_currency_stats(protocol::currency_stats_request) override {
      co_return protocol::currency_stats_response{};
   }

   boost::asio::awaitable<protocol::scheduled_response>
   get_scheduled_transactions(protocol::scheduled_request) override {
      co_return protocol::scheduled_response{};
   }

   boost::asio::awaitable<protocol::authorizers_response>
   get_accounts_by_authorizers(protocol::authorizers_request) override {
      co_return protocol::authorizers_response{};
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};

 private:
   protocol::state_point_response response_;
};

class transaction_implementation final : public chain_api::transaction {
 public:
   explicit transaction_implementation(protocol::transaction_read_only_response response)
       : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::transaction_status_response>
   get_status(protocol::transaction_status_request) override {
      co_return protocol::transaction_status_response{};
   }

   boost::asio::awaitable<protocol::transaction_status_response>
   await_transaction(protocol::transaction_await_request request) override {
      await_started.fetch_add(1, std::memory_order_release);
      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      timer.expires_after(std::chrono::milliseconds{request.timeout_ms});
      try {
         co_await timer.async_wait(boost::asio::use_awaitable);
      } catch (const boost::system::system_error& error) {
         if (error.code() == boost::asio::error::operation_aborted) {
            await_cancellations.fetch_add(1, std::memory_order_release);
         }
         throw;
      }
      await_deadlines.fetch_add(1, std::memory_order_release);
      FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::deadline_exceeded,
                            "fixture transaction wait reached its request deadline");
   }

   boost::asio::awaitable<std::vector<protocol::public_key>>
   get_required_keys(protocol::transaction_required_keys_request) override {
      co_return std::vector<protocol::public_key>{};
   }

   boost::asio::awaitable<protocol::transaction_read_only_response>
   compute_transaction(protocol::transaction_read_only_request request) override {
      last_audit.store(request.audit, std::memory_order_relaxed);
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   boost::asio::awaitable<protocol::transaction_read_only_response>
   send_read_only_transaction(protocol::transaction_read_only_request) override {
      co_return protocol::transaction_read_only_response{};
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};
   std::atomic<std::uint32_t> await_started{0};
   std::atomic<std::uint32_t> await_deadlines{0};
   std::atomic<std::uint32_t> await_cancellations{0};

 private:
   protocol::transaction_read_only_response response_;
};

class submission_implementation final : public chain_api::submission {
 public:
   boost::asio::awaitable<protocol::transaction_submit_response>
   submit(protocol::transaction_submit_request request) override {
      calls.fetch_add(1U, std::memory_order_relaxed);
      last_submit_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
      co_return protocol::transaction_submit_response{.id = request.transaction.id()};
   }

   boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(protocol::transaction_submit_batch_request request) override {
      calls.fetch_add(1U, std::memory_order_relaxed);
      last_batch_timeout_ms.store(request.timeout_ms, std::memory_order_relaxed);
      auto responses = std::vector<protocol::transaction_submit_response>{};
      responses.reserve(request.transactions.size());
      for (const auto& transaction : request.transactions) {
         responses.push_back(protocol::transaction_submit_response{.id = transaction.transaction.id()});
      }
      co_return responses;
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<std::uint64_t> last_submit_timeout_ms{0};
   std::atomic<std::uint64_t> last_batch_timeout_ms{0};
};

void wait_until(std::function<bool()> predicate, std::string_view failure) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }
   require(predicate(), failure);
}

void require_long_poll_transport(forge::asio::runtime& runtime,
                                 const forge::api::core::handle<chain_api::transaction>& remote,
                                 const std::shared_ptr<transaction_implementation>& owner, std::string_view transport,
                                 bool require_remote_cancellation) {
   const auto deadlines_before = owner->await_deadlines.load(std::memory_order_acquire);
   auto deadline_observed = false;
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 10})));
   } catch (const forge::chain::api::exceptions::deadline_exceeded&) {
      deadline_observed = true;
   }
   require(deadline_observed, std::string{transport} + " long-poll ignored its request deadline");
   require(owner->await_deadlines.load(std::memory_order_acquire) == deadlines_before + 1U,
           std::string{transport} + " deadline did not originate at the owner");

   const auto started_before = owner->await_started.load(std::memory_order_acquire);
   const auto cancellations_before = owner->await_cancellations.load(std::memory_order_acquire);
   auto cancellation = boost::asio::cancellation_signal{};
   auto pending = boost::asio::co_spawn(
       runtime.context(), remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'000}),
       boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   wait_until([&] { return owner->await_started.load(std::memory_order_acquire) > started_before; },
              std::string{transport} + " long-poll did not reach the owner");
   cancellation.emit(boost::asio::cancellation_type::all);
   auto caller_cancelled = false;
   try {
      static_cast<void>(pending.get());
   } catch (const forge::api::core::exceptions::cancelled&) {
      caller_cancelled = true;
   } catch (const forge::asio::exceptions::canceled&) {
      caller_cancelled = true;
   } catch (const std::exception& error) {
      require(false, std::string{transport} + " long-poll leaked a standard exception: " + error.what());
   }
   require(caller_cancelled, std::string{transport} + " long-poll did not return typed cancellation");
   if (require_remote_cancellation) {
      wait_until([&] { return owner->await_cancellations.load(std::memory_order_acquire) > cancellations_before; },
                 std::string{transport} + " long-poll cancellation did not reach the owner");
   }

   static_cast<void>(
       forge::asio::blocking::run(runtime, remote->get_status(forge::chain::protocol::transaction_status_request{})));
}

class admin_implementation final : public chain_api::admin {
 public:
   explicit admin_implementation(protocol::producer_status_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::push_block_response> push_block(protocol::signed_block) override {
      co_return protocol::push_block_response{};
   }

   boost::asio::awaitable<protocol::snapshot_response> create_snapshot(std::string) override {
      co_return protocol::snapshot_response{};
   }

   boost::asio::awaitable<protocol::prune_response> prune(protocol::prune_request request) override {
      error_calls.fetch_add(1, std::memory_order_relaxed);
      if (request.max_records == 0) {
         throw std::runtime_error{"chain API package test failure"};
      }
      co_return protocol::prune_response{};
   }

   boost::asio::awaitable<protocol::producer_status_response> producer_status(protocol::admin_query) override {
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   boost::asio::awaitable<protocol::supported_protocol_features_response>
   supported_protocol_features(protocol::supported_protocol_features_request) override {
      co_return protocol::supported_protocol_features_response{};
   }

   boost::asio::awaitable<protocol::ram_corrections_response>
   account_ram_corrections(protocol::ram_corrections_request) override {
      co_return protocol::ram_corrections_response{};
   }

   boost::asio::awaitable<protocol::unapplied_transactions_response>
   unapplied_transactions(protocol::unapplied_transactions_request) override {
      co_return protocol::unapplied_transactions_response{};
   }

   boost::asio::awaitable<protocol::snapshot_requests_response> snapshot_requests(protocol::admin_query) override {
      co_return protocol::snapshot_requests_response{};
   }

   boost::asio::awaitable<bool> configure_pause(protocol::producer_pause_request) override {
      co_return false;
   }

   boost::asio::awaitable<bool> update_runtime_options(protocol::producer_runtime_options) override {
      co_return false;
   }

   boost::asio::awaitable<bool> update_greylist(protocol::greylist_update_request) override {
      co_return false;
   }

   boost::asio::awaitable<bool> set_access_policy(protocol::producer_access_policy) override {
      co_return false;
   }

   boost::asio::awaitable<protocol::snapshot_schedule> schedule_snapshot(protocol::snapshot_schedule_request) override {
      co_return protocol::snapshot_schedule{};
   }

   boost::asio::awaitable<protocol::snapshot_schedule> unschedule_snapshot(protocol::snapshot_schedule_id) override {
      co_return protocol::snapshot_schedule{};
   }

   boost::asio::awaitable<protocol::integrity_hash_response> integrity_hash(protocol::admin_query) override {
      co_return protocol::integrity_hash_response{};
   }

   boost::asio::awaitable<bool> schedule_protocol_features(std::vector<protocol::digest>) override {
      co_return false;
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<std::uint32_t> error_calls{0};

 private:
   protocol::producer_status_response response_;
};

struct chain_api_services {
   std::shared_ptr<info_implementation> information;
   std::shared_ptr<block_implementation> blocks;
   std::shared_ptr<state_implementation> state;
   std::shared_ptr<transaction_implementation> transactions;
   std::shared_ptr<submission_implementation> submissions;
   std::shared_ptr<admin_implementation> administration;
};

struct http_responses {
   protocol::info_response information;
   protocol::block_state_response block;
   protocol::state_point_response state;
   protocol::transaction_read_only_response transaction;
   protocol::producer_status_response administration;
   bool oversized_request_rejected = false;
};

http_responses run_http_e2e(const chain_api_services& services) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::core::registry{};
   apis.install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                 services.information);
   apis.install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()), services.blocks);
   apis.install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()), services.state);
   apis.install<chain_api::transaction>(chain_api::limited_descriptor<chain_api::transaction>(package_limits()),
                                        services.transactions);
   apis.install<chain_api::submission>(chain_api::limited_descriptor<chain_api::submission>(package_limits()),
                                       services.submissions);
   apis.install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                  services.administration);

   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::info>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::block>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::state>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::transaction>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::submission>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::admin>()
                    .build());

   auto server = forge::net::http::server{
       runtime,
       forge::net::http::server_config{.max_request_body_bytes = 64U * 1024U},
       std::move(router),
   };
   forge::asio::blocking::run(runtime, server.async_start());
   require(server.port() != 0, "HTTP chain API server did not bind");

   auto responses = http_responses{};
   try {
      {
         auto limits_client = forge::net::http::client{
             runtime,
             forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
         };
         auto limits_remote =
             forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::transaction>(limits_client));
         auto submission_limits_remote =
             forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::submission>(limits_client));
         const auto started_before = services.transactions->await_started.load(std::memory_order_acquire);
         auto rejected = false;
         try {
            static_cast<void>(forge::asio::blocking::run(
                runtime, limits_remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001})));
         } catch (const forge::chain::api::exceptions::resource_exhausted&) {
            rejected = true;
         }
         require(rejected, "HTTP owner boundary accepted an oversized await deadline");
         require(services.transactions->await_started.load(std::memory_order_acquire) == started_before,
                 "HTTP oversized await deadline reached the owner");

         const auto submission_calls_before = services.submissions->calls.load(std::memory_order_acquire);
         auto submit_rejected = false;
         try {
            static_cast<void>(forge::asio::blocking::run(
                runtime, submission_limits_remote->submit(protocol::transaction_submit_request{
                             .timeout_ms = package_limits().max_await_ms + 1U,
                         })));
         } catch (const forge::chain::api::exceptions::resource_exhausted&) {
            submit_rejected = true;
         }
         require(submit_rejected, "HTTP owner boundary accepted an oversized submit deadline");

         auto zero_submit_rejected = false;
         try {
            static_cast<void>(forge::asio::blocking::run(
                runtime, submission_limits_remote->submit(protocol::transaction_submit_request{.timeout_ms = 0U})));
         } catch (const forge::chain::api::exceptions::invalid_request&) {
            zero_submit_rejected = true;
         }
         require(zero_submit_rejected, "HTTP owner boundary accepted a zero submit deadline");

         auto bounded_item = protocol::transaction_submit_request{.timeout_ms = 2'000U};
         auto bounded_transaction = protocol::signed_transaction{};
         bounded_transaction.expiration = protocol::time_point_sec{1U};
         bounded_item.transaction = protocol::packed_transaction{std::move(bounded_transaction)};
         const auto bounded_batch = forge::asio::blocking::run(
             runtime, submission_limits_remote->submit_batch(protocol::transaction_submit_batch_request{
                          .transactions = {std::move(bounded_item)},
                          .timeout_ms = 1'000U,
                      }));
         require(bounded_batch.size() == 1U, "HTTP batch deadline cap changed response cardinality");
         require(services.submissions->calls.load(std::memory_order_acquire) == submission_calls_before + 1U,
                 "HTTP batch deadline cap did not reach the owner");
         require(services.submissions->last_batch_timeout_ms.load(std::memory_order_relaxed) == 1'000U,
                 "HTTP batch deadline cap was not propagated to the owner");
      }
      auto client = forge::net::http::client{
          runtime,
          forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
      };
      auto info_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::info>(client));
      auto block_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::block>(client));
      auto state_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::state>(client));
      auto transaction_remote =
          forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::transaction>(client));
      auto submission_remote =
          forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::submission>(client));
      auto admin_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::admin>(client));
      responses.information = forge::asio::blocking::run(
          runtime, info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      responses.block = forge::asio::blocking::run(runtime, block_remote->get_block_state(protocol::block_request{
                                                                .num = 40,
                                                                .audit = protocol::audit_mode::required,
                                                            }));
      responses.state = forge::asio::blocking::run(runtime, state_remote->get_point(protocol::state_point_request{
                                                                .key = {0x01, 0x02, 0x03},
                                                                .audit = protocol::audit_mode::required,
                                                            }));
      responses.transaction = forge::asio::blocking::run(
          runtime, transaction_remote->compute_transaction(protocol::transaction_read_only_request{
                       .audit = protocol::audit_mode::required,
                   }));
      responses.administration =
          forge::asio::blocking::run(runtime, admin_remote->producer_status(protocol::admin_query{}));

      auto submission = chain_api::submission_client{std::move(submission_remote)};
      auto submitted = protocol::transaction_submit_request{.timeout_ms = 1'234U};
      submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
      const auto submitted_id = submitted.transaction.id();
      require(forge::asio::blocking::run(runtime, submission.submit(std::move(submitted))).id == submitted_id,
              "HTTP submission acknowledgement did not bind the submitted transaction");
      require(services.submissions->last_submit_timeout_ms.load(std::memory_order_relaxed) == 1'234U,
              "HTTP submission did not propagate its deadline");

      auto first_batch_item = protocol::transaction_submit_request{.timeout_ms = 1'000U};
      auto first_batch_transaction = protocol::signed_transaction{};
      first_batch_transaction.expiration = protocol::time_point_sec{1U};
      first_batch_item.transaction = protocol::packed_transaction{std::move(first_batch_transaction)};
      auto second_batch_item = protocol::transaction_submit_request{.timeout_ms = 2'000U};
      auto second_batch_transaction = protocol::signed_transaction{};
      second_batch_transaction.expiration = protocol::time_point_sec{2U};
      second_batch_item.transaction = protocol::packed_transaction{std::move(second_batch_transaction)};
      const auto batch_responses = forge::asio::blocking::run(
          runtime, submission.submit_batch(protocol::transaction_submit_batch_request{
                       .transactions = {std::move(first_batch_item), std::move(second_batch_item)},
                       .timeout_ms = 2'500U,
                   }));
      require(batch_responses.size() == 2U, "HTTP batch submission changed response cardinality");
      require(services.submissions->last_batch_timeout_ms.load(std::memory_order_relaxed) == 2'500U,
              "HTTP batch submission did not propagate its total deadline");
      require_long_poll_transport(runtime, transaction_remote, services.transactions, "HTTP", true);
      const auto calls_before_oversized = services.state->calls.load(std::memory_order_relaxed);
      try {
         static_cast<void>(forge::asio::blocking::run(runtime, state_remote->get_point(protocol::state_point_request{
                                                                   .key = protocol::bytes(70U * 1024U, 0x5aU),
                                                               })));
      } catch (const std::exception&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "HTTP chain API accepted an oversized request body");
      require(services.state->calls.load(std::memory_order_relaxed) == calls_before_oversized,
              "HTTP oversized request reached the owner service");
   } catch (...) {
      forge::asio::blocking::run(runtime, server.async_stop());
      throw;
   }

   forge::asio::blocking::run(runtime, server.async_stop());
   require(server.port() == 0, "HTTP chain API server remained bound after async_stop");
   return responses;
}

class chain_api_publisher final : public forge::app::plugin {
 public:
   forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "chain-api-publisher"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
          {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      auto plan =
          forge::api::core::binding()
              .serve(context.apis())
              .export_api<chain_api::info>({.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::block>({.id = {"forge.chain.api.block"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::state>({.id = {"forge.chain.api.state"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::transaction>(
                  {.id = {"forge.chain.api.transaction"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::submission>({.id = {"forge.chain.api.submission"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::admin>({.id = {"forge.chain.api.admin"}, .major = 1, .min_revision = 0})
              .build();
      resolver->publish_api(std::move(plan), forge::net::p2p::protocol_id{.value = std::string{chain_api_protocol}},
                            forge::plugins::p2p::resolver::publish_options{
                                .transport = forge::api::transport::options{.max_frame_size = chain_api_max_frame_size},
                            });
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class p2p_server_application final : public forge::app::application_shell {
 public:
   explicit p2p_server_application(chain_api_services services) : services_{std::move(services)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
          .id = forge::app::plugin_id{.value = "chain-api-publisher"},
          .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
          .factory = [] { return std::make_unique<chain_api_publisher>(); },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                              services_.information);
      context.apis().install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()),
                                               services_.blocks);
      context.apis().install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()),
                                               services_.state);
      context.apis().install<chain_api::transaction>(
          chain_api::limited_descriptor<chain_api::transaction>(package_limits()), services_.transactions);
      context.apis().install<chain_api::submission>(
          chain_api::limited_descriptor<chain_api::submission>(package_limits()), services_.submissions);
      context.apis().install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                               services_.administration);
      co_return;
   }

 private:
   chain_api_services services_;
};

class p2p_client_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
   }
};

forge::net::p2p::peer_id test_peer(std::uint8_t seed) {
   return forge::net::p2p::make_peer_id(
       {.type = forge::net::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, seed)});
}

forge::config::core::document p2p_config(const forge::net::p2p::peer_id& peer) {
   auto config = forge::config::core::document{};
   config.set("plugins.p2p.node.allow-insecure-test-mode", true);
   config.set("plugins.p2p.node.certificate-pem", std::string{chain_api_test::certificate});
   config.set("plugins.p2p.node.private-key-pem", std::string{chain_api_test::private_key});
   config.set("plugins.p2p.node.peer-id", peer.to_string());
   return config;
}

void shutdown_after_failure(forge::app::application_shell& application, bool started) noexcept {
   if (!started) {
      return;
   }
   application.request_stop();
   try {
      forge::asio::blocking::run(application.runtime(), application.shutdown());
   } catch (...) {
   }
}

void require_advertised_api(const auto& apis, std::string_view id) {
   for (const auto& api : apis) {
      if (api.id.value == id) {
         require(api.protocol == chain_api_protocol, "P2P resolver advertised a chain API on the wrong protocol");
         require(api.max_frame_size == chain_api_max_frame_size,
                 "P2P resolver advertised the wrong chain API frame limit");
         return;
      }
   }
   throw std::runtime_error{"P2P resolver omitted " + std::string{id}};
}

struct p2p_responses {
   protocol::info_response information;
   protocol::block_state_response block;
   protocol::state_point_response state;
   protocol::transaction_read_only_response transaction;
   protocol::producer_status_response administration;
   bool internal_error_preserved = false;
   bool oversized_request_rejected = false;
};

p2p_responses run_p2p_e2e(const chain_api_services& services) {
   const auto server_peer = test_peer(0x41);
   auto server_config = p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                                    forge::config::core::value{
                                                        "/ip4/127.0.0.1/udp/0/quic-v1",
                                                    },
                                                });

   auto server = p2p_server_application{services};
   auto client = p2p_client_application{};
   auto server_started = false;
   auto client_started = false;

   try {
      server.configure(server_config);
      forge::asio::blocking::run(server.runtime(), server.startup());
      server_started = true;

      auto server_node = server.apis().get<forge::plugins::p2p::node::api>(
          {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
      const auto server_endpoint = server_node->local_endpoint();
      require(server_endpoint.has_value(), "P2P chain API server did not publish a local endpoint");

      auto client_config = p2p_config(test_peer(0x42));
      client_config.set("plugins.p2p.node.bootstrap", forge::config::core::value::array_type{
                                                          forge::config::core::value{server_endpoint->to_string()},
                                                      });
      client.configure(client_config);
      forge::asio::blocking::run(client.runtime(), client.startup());
      client_started = true;

      auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
          {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      const auto remote_apis = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
      require(remote_apis.size() == 6, "P2P resolver did not advertise all six chain APIs");
      require_advertised_api(remote_apis, "forge.chain.api.info");
      require_advertised_api(remote_apis, "forge.chain.api.block");
      require_advertised_api(remote_apis, "forge.chain.api.state");
      require_advertised_api(remote_apis, "forge.chain.api.transaction");
      require_advertised_api(remote_apis, "forge.chain.api.submission");
      require_advertised_api(remote_apis, "forge.chain.api.admin");

      const auto resolution = forge::asio::blocking::run(
          client.runtime(),
          resolver->resolve(server_peer, {.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0}));
      require(resolution.api.protocol == chain_api_protocol, "P2P resolver selected the wrong chain API protocol");

      auto limits_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::transaction>(server_peer));
      const auto started_before = services.transactions->await_started.load(std::memory_order_acquire);
      auto limit_rejected = false;
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(),
             limits_remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001})));
      } catch (const forge::chain::api::exceptions::resource_exhausted&) {
         limit_rejected = true;
      }
      require(limit_rejected, "P2P owner boundary accepted an oversized await deadline");
      require(services.transactions->await_started.load(std::memory_order_acquire) == started_before,
              "P2P oversized await deadline reached the owner");

      auto info_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::info>(server_peer));
      auto block_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::block>(server_peer));
      auto state_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::state>(server_peer));
      auto transaction_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::transaction>(server_peer));
      auto submission_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::submission>(server_peer));
      auto admin_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::admin>(server_peer));

      auto responses = p2p_responses{};
      responses.information = forge::asio::blocking::run(
          client.runtime(), info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      responses.block =
          forge::asio::blocking::run(client.runtime(), block_remote->get_block_state(protocol::block_request{
                                                           .num = 40,
                                                           .audit = protocol::audit_mode::required,
                                                       }));
      responses.state =
          forge::asio::blocking::run(client.runtime(), state_remote->get_point(protocol::state_point_request{
                                                           .key = {0x01, 0x02, 0x03},
                                                           .audit = protocol::audit_mode::required,
                                                       }));
      responses.transaction = forge::asio::blocking::run(
          client.runtime(), transaction_remote->compute_transaction(protocol::transaction_read_only_request{
                                .audit = protocol::audit_mode::required,
                            }));
      auto submission = chain_api::submission_client{std::move(submission_remote)};
      auto submitted = protocol::transaction_submit_request{};
      submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
      const auto submitted_id = submitted.transaction.id();
      require(forge::asio::blocking::run(client.runtime(), submission.submit(std::move(submitted))).id == submitted_id,
              "P2P submission acknowledgement did not bind the submitted transaction");
      responses.administration =
          forge::asio::blocking::run(client.runtime(), admin_remote->producer_status(protocol::admin_query{}));
      require_long_poll_transport(client.runtime(), transaction_remote, services.transactions, "P2P", true);
      const auto calls_before_oversized = services.state->calls.load(std::memory_order_relaxed);
      try {
         static_cast<void>(
             forge::asio::blocking::run(client.runtime(), state_remote->get_point(protocol::state_point_request{
                                                              .key = protocol::bytes(70U * 1024U, 0x5aU),
                                                          })));
      } catch (const std::exception&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "P2P chain API accepted an oversized request frame");
      require(services.state->calls.load(std::memory_order_relaxed) == calls_before_oversized,
              "P2P oversized request reached the owner service");
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(), admin_remote->prune(protocol::prune_request{.through_block = 40, .max_records = 0})));
      } catch (const forge::api::core::exceptions::remote_internal&) {
         responses.internal_error_preserved = true;
      }
      require(responses.internal_error_preserved, "P2P chain API did not preserve remote error semantics");

      auto stop_thread = std::thread{[&client] { client.request_stop(); }};
      stop_thread.join();
      const auto shutdown_started = std::chrono::steady_clock::now();
      forge::asio::blocking::run(client.runtime(), client.shutdown());
      const auto shutdown_elapsed = std::chrono::steady_clock::now() - shutdown_started;
      client_started = false;
      require(shutdown_elapsed < std::chrono::seconds{2}, "P2P resolver client shutdown did not cancel promptly");
      require(client.state() == forge::app::application_state::stopped,
              "P2P resolver client did not reach stopped state");

      server.request_stop();
      forge::asio::blocking::run(server.runtime(), server.shutdown());
      server_started = false;
      require(server.state() == forge::app::application_state::stopped,
              "P2P chain API server did not reach stopped state");
      return responses;
   } catch (...) {
      shutdown_after_failure(client, client_started);
      shutdown_after_failure(server, server_started);
      throw;
   }
}

void require_audit_semantics(const protocol::audited_response& response) {
   require(response.context.anchor.has_value(), "transport dropped the audit anchor");
   require(response.audit.has_value(), "transport dropped the audit bundle");
   require(response.audit->finality.has_value(), "transport dropped the finality proof");
   require(response.audit->finality->payload == protocol::bytes{0x01, 0x02, 0x03},
           "transport changed finality proof bytes");
   require(response.audit->state.size() == 2, "transport changed state proof cardinality");
   require(response.audit->state[1].scheme == "forge.db.authenticated.range",
           "transport changed ranked range proof metadata");
   require(response.audit->transaction.has_value(), "transport dropped transaction inclusion proof");
   require(response.audit->transaction->path.size() == 2, "transport changed transaction proof path");
}

} // namespace

int main() {
   static_assert(std::is_abstract_v<forge::chain::api::info>);
   static_assert(std::is_abstract_v<forge::chain::api::block>);
   static_assert(std::is_abstract_v<forge::chain::api::state>);
   static_assert(std::is_abstract_v<forge::chain::api::transaction>);
   static_assert(std::is_abstract_v<forge::chain::api::submission>);
   static_assert(std::is_abstract_v<forge::chain::api::admin>);
   static_assert(!std::is_abstract_v<forge::chain::api::submission_client>);
   static_assert(std::derived_from<forge::chain::api::authenticated_audit_verifier, forge::chain::api::audit_verifier>);
   static_assert(std::is_abstract_v<forge::chain::api::finality_verifier>);
   static_assert(std::is_same_v<decltype(protocol::table_rows_response{}.rows), std::vector<protocol::table_row>>);
   static_assert(std::is_same_v<decltype(protocol::table_rows_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_response{}.next), std::optional<protocol::bytes>>);

   const auto finality_delegate = std::make_shared<accepting_finality>();
   const auto finality = std::make_shared<chain_api::cached_finality_verifier>(finality_delegate, 4U);
   const auto anchor = protocol::state_anchor{
       .chain = hash("package-verifier-chain"),
       .block = hash("package-verifier-anchor"),
   };
   finality->verify(anchor, {});
   require(finality_delegate->calls == 1U, "installed cached finality verifier did not invoke its delegate");
   const auto audit = std::make_shared<chain_api::authenticated_audit_verifier>(
       chain_api::authenticated_audit_options{.chain = anchor.chain, .state_domain = "package-test"}, finality);
   audit->verify_context(protocol::response_context{.chain = anchor.chain});
   auto audit_anchor = anchor;
   audit_anchor.block = hash("package-audit-anchor");
   audit->verify_finality(audit_anchor, {});
   require(finality_delegate->calls == 2U, "installed authenticated verifier did not invoke finality verification");
   const auto projections = std::make_shared<chain_api::projection_verifier>();
   auto projection_rejected = false;
   try {
      projections->verify(protocol::block_request{}, protocol::block_state_response{}, protocol::audit_bundle{},
                          *audit);
   } catch (const chain_api::exceptions::audit_not_supported&) {
      projection_rejected = true;
   }
   require(projection_rejected, "installed default projection verifier did not fail closed");
   auto verified = chain_api::verified_client{chain_api::raw_client{chain_api::service_handles{}}, audit, projections};
   auto verifier_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto verified_rejected = false;
   try {
      static_cast<void>(forge::asio::blocking::run(verifier_runtime, verified.get_info()));
   } catch (const chain_api::exceptions::unavailable&) {
      verified_rejected = true;
   }
   require(verified_rejected, "installed verified client did not reject a missing transport");

   auto request = protocol::state_point_request{};
   auto block = protocol::block_request{};
   auto transaction = protocol::transaction_status_request{};
   const auto table_key = forge::chain::api::encode_table_key(std::uint64_t{42U});
   (void)request;
   (void)block;
   (void)transaction;
   require(table_key.size() == sizeof(std::uint64_t), "installed table key codec returned the wrong width");

   const auto expected_info = make_info_response();
   const auto expected_block = make_block_state_response(expected_info);
   const auto expected_state = make_state_point_response(expected_info);
   const auto expected_transaction = make_transaction_response(expected_info);
   const auto expected_admin = make_admin_response();
   const auto services = chain_api_services{
       .information = std::make_shared<info_implementation>(expected_info),
       .blocks = std::make_shared<block_implementation>(expected_block),
       .state = std::make_shared<state_implementation>(expected_state),
       .transactions = std::make_shared<transaction_implementation>(expected_transaction),
       .submissions = std::make_shared<submission_implementation>(),
       .administration = std::make_shared<admin_implementation>(expected_admin),
   };

   const auto http_response = run_http_e2e(services);
   const auto p2p_response = run_p2p_e2e(services);

   require(http_response.information == expected_info, "HTTP info API changed chain audit DTO semantics");
   require(http_response.information.head_time == expected_info.head_time, "HTTP info API lost head time microseconds");
   require(http_response.information.finalized_time == expected_info.finalized_time,
           "HTTP info API lost finalized time microseconds");
   require(http_response.block == expected_block, "HTTP block API changed typed DTO semantics");
   require(http_response.state == expected_state, "HTTP state API changed chain audit DTO semantics");
   require(http_response.transaction == expected_transaction, "HTTP transaction API changed typed DTO semantics");
   require(http_response.administration == expected_admin, "HTTP admin API changed typed DTO semantics");
   require(http_response.oversized_request_rejected, "HTTP transport limit was not exercised");
   require(p2p_response.information == expected_info, "P2P info API changed chain audit DTO semantics");
   require(p2p_response.block == expected_block, "P2P block API changed typed DTO semantics");
   require(p2p_response.state == expected_state, "P2P state API changed typed DTO semantics");
   require(p2p_response.transaction == expected_transaction, "P2P transaction API changed typed DTO semantics");
   require(p2p_response.administration == expected_admin, "P2P admin API changed typed DTO semantics");
   require(p2p_response.oversized_request_rejected, "P2P transport limit was not exercised");
   require(http_response.information == p2p_response.information, "HTTP and P2P info API responses diverged");
   require(http_response.block == p2p_response.block, "HTTP and P2P block API responses diverged");
   require(http_response.state == p2p_response.state, "HTTP and P2P state API responses diverged");
   require(http_response.transaction == p2p_response.transaction, "HTTP and P2P transaction API responses diverged");
   require(http_response.administration == p2p_response.administration, "HTTP and P2P admin API responses diverged");
   require(forge::raw::pack(http_response.information) == forge::raw::pack(p2p_response.information),
           "HTTP and P2P info canonical bytes diverged");
   require(forge::raw::pack(http_response.block) == forge::raw::pack(p2p_response.block),
           "HTTP and P2P block canonical bytes diverged");
   require(forge::raw::pack(http_response.state) == forge::raw::pack(p2p_response.state),
           "HTTP and P2P state canonical bytes diverged");
   require(forge::raw::pack(http_response.transaction) == forge::raw::pack(p2p_response.transaction),
           "HTTP and P2P transaction canonical bytes diverged");
   require(forge::raw::pack(http_response.administration) == forge::raw::pack(p2p_response.administration),
           "HTTP and P2P admin canonical bytes diverged");
   require_audit_semantics(http_response.information);
   require_audit_semantics(http_response.block);
   require_audit_semantics(http_response.state);
   require_audit_semantics(http_response.transaction);
   require_audit_semantics(p2p_response.information);
   require_audit_semantics(p2p_response.block);
   require_audit_semantics(p2p_response.state);
   require_audit_semantics(p2p_response.transaction);
   require(services.information->calls.load(std::memory_order_relaxed) == 2,
           "info transport E2E did not dispatch both typed calls");
   require(services.blocks->calls.load(std::memory_order_relaxed) == 2,
           "transport E2E did not dispatch both block typed calls");
   require(services.state->calls.load(std::memory_order_relaxed) == 2,
           "state transport E2E did not dispatch both typed calls");
   require(services.transactions->calls.load(std::memory_order_relaxed) == 2,
           "transport E2E did not dispatch both transaction typed calls");
   require(services.administration->calls.load(std::memory_order_relaxed) == 2,
           "transport E2E did not dispatch both admin typed calls");
   require(services.administration->error_calls.load(std::memory_order_relaxed) == 1,
           "P2P admin API did not dispatch its typed error call");
   require(services.information->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required,
           "info transport E2E changed the requested audit mode");
   require(services.blocks->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required,
           "P2P block API changed the requested audit mode");
   require(services.state->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required,
           "P2P state API changed the requested audit mode");
   require(services.transactions->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required,
           "P2P transaction API changed the requested audit mode");
   return 0;
}
