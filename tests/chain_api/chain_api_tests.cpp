#include <boost/test/unit_test.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <future>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <forge/exceptions/macros.hpp>

import forge.api.core.connection;
import forge.api.core.exceptions;
import forge.api.core.registry;
import forge.api.http.client_response;
import forge.api.http.mapping;
import forge.api.http.openapi;
import forge.asio.exceptions;
import forge.chain.api.admin;
import forge.chain.api.authenticated_audit_verifier;
import forge.chain.api.block;
import forge.chain.api.exceptions;
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
import forge.chain.core.merkle;
import forge.chain.protocol.audit;
import forge.crypto.digest.sha256;
import forge.db.authenticated.codec;
import forge.db.authenticated.hash;
import forge.net.http.types;
import forge.raw.raw;
import forge.schema.exceptions;
import forge.schema.scalar;

namespace {

template <typename Client>
concept exposes_raw_client = requires(Client& client) { client.raw(); };

template <typename Client>
concept exposes_submission = requires(Client& client, forge::chain::protocol::transaction_submit_request request) {
   client.submit(std::move(request));
};

template <typename Client>
concept exposes_indirect_submission =
    requires(Client& client, forge::chain::protocol::transaction_submit_request request) {
       client.transactions().submit(std::move(request));
    };

template <typename Client>
concept exposes_administration = requires(Client& client) { client.admin(); };

static_assert(!exposes_raw_client<forge::chain::api::verified_client>);
static_assert(!exposes_administration<forge::chain::api::raw_client>);
static_assert(!exposes_submission<forge::chain::api::verified_client>);
static_assert(exposes_submission<forge::chain::api::submission_client>);
static_assert(!exposes_submission<forge::chain::api::transaction>);
static_assert(!exposes_indirect_submission<forge::chain::api::raw_client>);

using forge::api::http::cache_policy;
using forge::api::http::route;
using forge::net::http::method;
namespace authenticated = forge::db::authenticated;

static_assert(std::same_as<decltype(forge::chain::protocol::transaction_read_only_request{}.transaction),
                           forge::chain::protocol::packed_transaction>);

template <typename T> T run(boost::asio::awaitable<T> operation) {
   auto context = boost::asio::io_context{};
   auto result = boost::asio::co_spawn(context, std::move(operation), boost::asio::use_future);
   context.run();
   return result.get();
}

class block_service final : public forge::chain::api::block {
 public:
   explicit block_service(forge::chain::protocol::block_response response) : response_{std::move(response)} {}
   explicit block_service(forge::chain::protocol::block_header_response response)
       : header_response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::block_response>
   get_block(forge::chain::protocol::block_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::block_header_response>
   get_header(forge::chain::protocol::block_request) override {
      co_return header_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::block_state_response>
   get_block_state(forge::chain::protocol::block_request) override {
      co_return forge::chain::protocol::block_state_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::block_range_response>
   get_canonical_range(forge::chain::protocol::block_range_request) override {
      co_return forge::chain::protocol::block_range_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::protocol_features_response>
   get_activated_protocol_features(forge::chain::protocol::protocol_features_request) override {
      co_return forge::chain::protocol::protocol_features_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::consensus_parameters_response>
   get_consensus_parameters(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::consensus_parameters_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::producers_response>
   get_producers(forge::chain::protocol::producers_request) override {
      co_return forge::chain::protocol::producers_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::producer_schedule_response>
   get_producer_schedule(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::producer_schedule_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::finalizer_info_response>
   get_finalizer_info(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::finalizer_info_response{};
   }

 private:
   forge::chain::protocol::block_response response_;
   forge::chain::protocol::block_header_response header_response_;
};

class info_service final : public forge::chain::api::info {
 public:
   explicit info_service(forge::chain::protocol::info_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::info_response>
   get(forge::chain::protocol::anchored_request) override {
      co_return response_;
   }

 private:
   forge::chain::protocol::info_response response_;
};

class state_service final : public forge::chain::api::state {
 public:
   explicit state_service(forge::chain::protocol::state_changes_response response) : response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::state_point_response response)
       : point_response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::state_range_response response)
       : range_response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::account_response response) : account_response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::state_point_response>
   get_point(forge::chain::protocol::state_point_request request) override {
      last_point_request = std::move(request);
      if (point_failure == failure::standard) {
         throw std::runtime_error{"test state service failure"};
      }
      if (point_failure == failure::nonstandard) {
         throw 7;
      }
      if (point_failure == failure::canceled) {
         throw boost::system::system_error{boost::asio::error::operation_aborted};
      }
      if (point_failure == failure::timed_out) {
         throw boost::system::system_error{boost::asio::error::timed_out};
      }
      if (point_failure == failure::api_canceled) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "test API transport cancellation");
      }
      if (point_failure == failure::api_deadline) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::deadline_exceeded, "test API transport deadline");
      }
      if (point_failure == failure::foreign_forge) {
         FORGE_THROW_EXCEPTION(forge::asio::exceptions::internal, "test foreign Forge service failure");
      }
      if (point_failure == failure::chain_api) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_request, "test Chain API failure");
      }
      co_return point_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::state_range_response>
   get_range(forge::chain::protocol::state_range_request) override {
      co_return range_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::state_changes_response>
   get_changes(forge::chain::protocol::state_changes_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::account_response>
   get_account(forge::chain::protocol::account_request) override {
      co_return account_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::code_response>
   get_code(forge::chain::protocol::code_request) override {
      co_return forge::chain::protocol::code_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::table_rows_response>
   get_table_rows(forge::chain::protocol::table_rows_request) override {
      co_return forge::chain::protocol::table_rows_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::table_scope_response>
   get_table_scope(forge::chain::protocol::table_scope_request) override {
      co_return forge::chain::protocol::table_scope_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::currency_balance_response>
   get_currency_balance(forge::chain::protocol::currency_balance_request) override {
      co_return forge::chain::protocol::currency_balance_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::currency_stats_response>
   get_currency_stats(forge::chain::protocol::currency_stats_request) override {
      co_return forge::chain::protocol::currency_stats_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::scheduled_response>
   get_scheduled_transactions(forge::chain::protocol::scheduled_request) override {
      co_return forge::chain::protocol::scheduled_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::authorizers_response>
   get_accounts_by_authorizers(forge::chain::protocol::authorizers_request) override {
      co_return forge::chain::protocol::authorizers_response{};
   }

   std::optional<forge::chain::protocol::state_point_request> last_point_request;
   enum class failure : std::uint8_t {
      none,
      standard,
      nonstandard,
      canceled,
      timed_out,
      api_canceled,
      api_deadline,
      foreign_forge,
      chain_api
   };
   failure point_failure = failure::none;

 private:
   forge::chain::protocol::state_point_response point_response_;
   forge::chain::protocol::state_range_response range_response_;
   forge::chain::protocol::state_changes_response response_;
   forge::chain::protocol::account_response account_response_;
};

class transaction_service final : public forge::chain::api::transaction {
 public:
   explicit transaction_service(forge::chain::protocol::transaction_status_response response)
       : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::transaction_status_response>
   get_status(forge::chain::protocol::transaction_status_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_status_response>
   await_transaction(forge::chain::protocol::transaction_await_request) override {
      co_return response_;
   }

   boost::asio::awaitable<std::vector<forge::chain::protocol::public_key>>
   get_required_keys(forge::chain::protocol::transaction_required_keys_request) override {
      co_return std::vector<forge::chain::protocol::public_key>{};
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_read_only_response>
   compute_transaction(forge::chain::protocol::transaction_read_only_request) override {
      co_return forge::chain::protocol::transaction_read_only_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_read_only_response>
   send_read_only_transaction(forge::chain::protocol::transaction_read_only_request) override {
      co_return forge::chain::protocol::transaction_read_only_response{};
   }

 private:
   forge::chain::protocol::transaction_status_response response_;
};

class submission_service final : public forge::chain::api::submission {
 public:
   explicit submission_service(std::vector<forge::chain::protocol::transaction_submit_response> responses)
       : responses_{std::move(responses)} {}

   boost::asio::awaitable<forge::chain::protocol::transaction_submit_response>
   submit(forge::chain::protocol::transaction_submit_request) override {
      if (throw_standard) {
         throw std::runtime_error{"test submission service failure"};
      }
      if (throw_forge) {
         FORGE_THROW_EXCEPTION(forge::asio::exceptions::internal, "test foreign Forge submission failure");
      }
      if (throw_timed_out) {
         throw boost::system::system_error{boost::asio::error::timed_out};
      }
      if (throw_api_canceled) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "test API submission cancellation");
      }
      if (throw_api_deadline) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::deadline_exceeded, "test API submission deadline");
      }
      if (responses_.empty()) {
         co_return forge::chain::protocol::transaction_submit_response{};
      }
      co_return responses_.front();
   }

   boost::asio::awaitable<std::vector<forge::chain::protocol::transaction_submit_response>>
   submit_batch(forge::chain::protocol::transaction_submit_batch_request) override {
      if (throw_api_canceled) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::cancelled, "test API batch submission cancellation");
      }
      if (throw_api_deadline) {
         FORGE_THROW_EXCEPTION(forge::api::core::exceptions::deadline_exceeded, "test API batch submission deadline");
      }
      co_return responses_;
   }

 private:
   std::vector<forge::chain::protocol::transaction_submit_response> responses_;

 public:
   bool throw_standard = false;
   bool throw_forge = false;
   bool throw_timed_out = false;
   bool throw_api_canceled = false;
   bool throw_api_deadline = false;
};

class deadline_remote_invoker final : public forge::api::core::remote_invoker {
 public:
   boost::asio::awaitable<forge::api::core::response> async_call(forge::api::core::request value) override {
      ++calls;
      const auto descriptor = forge::chain::api::transaction::describe();
      const auto* method = forge::api::core::find_method(descriptor, value.method);
      if (method == nullptr) {
         throw forge::api::core::exceptions::protocol_error{"remote test method descriptor is missing"};
      }
      const auto error =
          forge::chain::api::exceptions::deadline_exceeded{"remote transaction wait reached its deadline"};
      co_return forge::api::core::response{
          .api = std::move(value.api),
          .method = std::move(value.method),
          .error = forge::api::core::project_error(*method, error),
      };
   }

   std::size_t calls = 0;
};

class accepting_audit_verifier final : public forge::chain::api::audit_verifier {
 public:
   [[nodiscard]] std::optional<forge::chain::protocol::block_id> preferred_finality_anchor() const override {
      if (throw_standard_preferred_anchor) {
         throw std::runtime_error{"test preferred anchor failure"};
      }
      return preferred_anchor;
   }

   void verify_context(const forge::chain::protocol::response_context&) override {
      if (throw_standard_context) {
         throw std::runtime_error{"test context verifier failure"};
      }
   }
   void verify_finality(const forge::chain::protocol::state_anchor&,
                        const forge::chain::protocol::proof_blob&) override {}
   std::optional<forge::chain::protocol::bytes> verify_state_point(const forge::chain::protocol::state_anchor&,
                                                                   const forge::chain::protocol::state_point_request&,
                                                                   const forge::chain::protocol::proof_blob&) override {
      ++state_point_verifications;
      if (throw_nonstandard_state_point) {
         throw 7;
      }
      return point_value;
   }
   forge::chain::protocol::state_range_response verify_state_range(const forge::chain::protocol::state_anchor&,
                                                                   const forge::chain::protocol::state_range_request&,
                                                                   const forge::chain::protocol::proof_blob&) override {
      ++state_range_verifications;
      return range_result;
   }
   forge::chain::protocol::state_change_range verify_state_changes(const forge::chain::protocol::state_anchor&,
                                                                   const forge::chain::protocol::key_range& range,
                                                                   std::uint32_t,
                                                                   const forge::chain::protocol::proof_blob&) override {
      ++state_change_verifications;
      if (state_change_result) {
         auto result = *state_change_result;
         result.range = range;
         return result;
      }
      return forge::chain::protocol::state_change_range{.range = range};
   }
   void verify_ancestry(const forge::chain::protocol::state_anchor& finalized,
                        std::span<const forge::chain::protocol::state_anchor> intermediate,
                        const forge::chain::protocol::proof_blob& proof) override {
      ++ancestry_verifications;
      ancestry_finalized = finalized;
      ancestry_intermediate.assign(intermediate.begin(), intermediate.end());
      ancestry_proof = proof;
   }
   void verify_transaction(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::transaction_id&,
                           const forge::chain::protocol::transaction_status_response&,
                           const forge::chain::protocol::transaction_inclusion_proof&) override {
      ++transaction_verifications;
      if (throw_standard_transaction) {
         throw std::runtime_error{"test transaction verifier failure"};
      }
   }

   std::size_t state_point_verifications = 0;
   std::size_t state_range_verifications = 0;
   std::size_t state_change_verifications = 0;
   std::size_t transaction_verifications = 0;
   std::size_t ancestry_verifications = 0;
   std::optional<forge::chain::protocol::bytes> point_value;
   forge::chain::protocol::state_range_response range_result;
   std::optional<forge::chain::protocol::state_change_range> state_change_result;
   std::optional<forge::chain::protocol::state_anchor> ancestry_finalized;
   std::vector<forge::chain::protocol::state_anchor> ancestry_intermediate;
   std::optional<forge::chain::protocol::proof_blob> ancestry_proof;
   bool throw_standard_context = false;
   bool throw_nonstandard_state_point = false;
   bool throw_standard_transaction = false;
   bool throw_standard_preferred_anchor = false;
   std::optional<forge::chain::protocol::block_id> preferred_anchor;
};

class account_projection_verifier final : public forge::chain::api::projection_verifier {
 public:
   void verify(const forge::chain::protocol::account_request& request,
               const forge::chain::protocol::account_response& response,
               const forge::chain::protocol::audit_bundle& audit,
               forge::chain::api::audit_verifier& verifier) override {
      if (!response.context.anchor || audit.state.size() != 1U) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_state_proof,
                               "test account projection requires one authenticated source");
      }
      ++verifications;
      const auto value = verifier.verify_state_point(*response.context.anchor,
                                                     forge::chain::protocol::state_point_request{
                                                         .key = {9U},
                                                         .anchor = request.anchor,
                                                         .audit = forge::chain::protocol::audit_mode::required,
                                                     },
                                                     audit.state.front());
      if (value != forge::chain::protocol::bytes{1U, 2U}) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_state_proof,
                               "test account projection rejected its authenticated source");
      }
   }

   std::size_t verifications = 0;
};

class throwing_account_projection_verifier final : public forge::chain::api::projection_verifier {
 public:
   void verify(const forge::chain::protocol::account_request&, const forge::chain::protocol::account_response&,
               const forge::chain::protocol::audit_bundle&, forge::chain::api::audit_verifier&) override {
      if (nonstandard) {
         throw 7;
      }
      throw std::runtime_error{"test projection verifier failure"};
   }

   bool nonstandard = false;
};

class recording_finality_verifier final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) override {
      ++verify_calls;
      if (throw_nonstandard) {
         throw 7;
      }
      if (failures_remaining != 0U) {
         --failures_remaining;
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_finality,
                               "test finality delegate rejected anchor");
      }
   }

   void verify_ancestry(const forge::chain::protocol::state_anchor& finalized,
                        std::span<const forge::chain::protocol::state_anchor> intermediate,
                        const forge::chain::protocol::proof_blob& proof) override {
      ++ancestry_calls;
      if (throw_nonstandard) {
         throw 7;
      }
      ancestry_finalized = finalized;
      ancestry_intermediate.assign(intermediate.begin(), intermediate.end());
      ancestry_proof = proof;
   }

   std::size_t verify_calls = 0;
   std::size_t ancestry_calls = 0;
   std::size_t failures_remaining = 0;
   bool throw_nonstandard = false;
   std::optional<forge::chain::protocol::state_anchor> ancestry_finalized;
   std::vector<forge::chain::protocol::state_anchor> ancestry_intermediate;
   std::optional<forge::chain::protocol::proof_blob> ancestry_proof;
};

class blocking_finality_verifier final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) override {
      const auto call = verify_calls.fetch_add(1U) + 1U;
      if (call != 1U) {
         return;
      }

      auto lock = std::unique_lock{mutex_};
      entered_ = true;
      entered_condition_.notify_all();
      release_condition_.wait(lock, [this] { return released_; });
   }

   void verify_ancestry(const forge::chain::protocol::state_anchor&,
                        std::span<const forge::chain::protocol::state_anchor>,
                        const forge::chain::protocol::proof_blob&) override {}

   void wait_until_entered() {
      auto lock = std::unique_lock{mutex_};
      entered_condition_.wait(lock, [this] { return entered_; });
   }

   void release() {
      {
         const auto lock = std::lock_guard{mutex_};
         released_ = true;
      }
      release_condition_.notify_all();
   }

   std::atomic<std::size_t> verify_calls = 0;

 private:
   std::mutex mutex_;
   std::condition_variable entered_condition_;
   std::condition_variable release_condition_;
   bool entered_ = false;
   bool released_ = false;
};

class standard_throwing_finality_verifier final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) override {
      throw std::runtime_error{"test finality implementation failure"};
   }

   void verify_ancestry(const forge::chain::protocol::state_anchor&,
                        std::span<const forge::chain::protocol::state_anchor>,
                        const forge::chain::protocol::proof_blob&) override {
      throw std::runtime_error{"test ancestry implementation failure"};
   }
};

forge::chain::protocol::state_anchor make_finality_anchor() {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.chain._hash[0] = 1U;
   anchor.block._hash[0] = 2U;
   anchor.block_num = 3U;
   anchor.transaction_root._hash[0] = 4U;
   anchor.state_root._hash[0] = 5U;
   anchor.state_size = 6U;
   anchor.change_root._hash[0] = 7U;
   anchor.change_count = 8U;
   return anchor;
}

authenticated::bytes authenticated_bytes(std::string_view value) {
   auto result = authenticated::bytes{};
   result.reserve(value.size());
   for (const auto character : value) {
      result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
   }
   return result;
}

forge::chain::protocol::bytes protocol_bytes(std::span<const std::byte> value) {
   auto result = forge::chain::protocol::bytes{};
   result.reserve(value.size());
   for (const auto byte : value) {
      result.push_back(std::to_integer<std::uint8_t>(byte));
   }
   return result;
}

forge::chain::protocol::bytes protocol_bytes(std::string_view value) {
   const auto encoded = authenticated_bytes(value);
   return protocol_bytes(encoded);
}

authenticated::proof_leaf authenticated_leaf(std::string_view key, std::string_view value) {
   auto encoded = authenticated_bytes(value);
   return {
       .key = authenticated_bytes(key),
       .value_hash = authenticated::hash_value(encoded),
       .value = std::move(encoded),
   };
}

authenticated::proof_leaf authenticated_change_leaf(const authenticated::mutation& mutation) {
   auto encoded = authenticated::encode_change_value(mutation);
   return {
       .key = mutation.key,
       .value_hash = authenticated::hash_value(encoded),
       .value = std::move(encoded),
   };
}

authenticated::digest authenticated_leaf_hash(std::string_view tree_domain, const authenticated::proof_leaf& value) {
   return authenticated::hash_leaf(tree_domain, value.key, value.value_hash);
}

authenticated::digest authenticated_branch_hash(std::string_view tree_domain,
                                                const authenticated::proof_branch& value) {
   return authenticated::hash_inner(tree_domain, value.height, value.size, value.min_key, value.max_key,
                                    value.separator, value.left_hash, value.right_hash);
}

forge::chain::protocol::chain_id authenticated_chain() {
   auto chain = forge::chain::protocol::chain_id{};
   chain._hash[0] = 0x41U;
   return chain;
}

forge::chain::protocol::state_anchor authenticated_anchor(const authenticated::root& value) {
   auto result = forge::chain::protocol::state_anchor{
       .chain = authenticated_chain(),
       .block_num = static_cast<std::uint32_t>(value.version),
       .state_root = value.state_root,
       .state_size = value.state_size,
       .change_root = value.change_root,
       .change_count = value.change_count,
   };
   result.block._hash[0] = 0x42U;
   return result;
}

template <typename Proof>
forge::chain::protocol::proof_blob authenticated_proof_blob(std::string scheme, const Proof& proof) {
   const auto encoded = authenticated::encode(proof);
   return {
       .scheme = std::move(scheme),
       .version = 1U,
       .payload = protocol_bytes(encoded),
   };
}

struct authenticated_point_fixture {
   std::string domain;
   authenticated::proof_leaf alpha;
   authenticated::proof_leaf gamma;
   authenticated::root root;
   std::vector<authenticated::proof_step> path;
};

authenticated_point_fixture make_authenticated_point_fixture() {
   auto fixture = authenticated_point_fixture{
       .domain = "forge.test.chain-api.authenticated-point.v3",
       .alpha = authenticated_leaf("alpha", "one"),
       .gamma = authenticated_leaf("gamma", "three"),
   };
   const auto state_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::state);
   const auto change_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::changes);
   fixture.root = {
       .version = 41U,
       .state_root = authenticated::hash_inner(state_domain, 1U, 2U, fixture.alpha.key, fixture.gamma.key,
                                               fixture.gamma.key, authenticated_leaf_hash(state_domain, fixture.alpha),
                                               authenticated_leaf_hash(state_domain, fixture.gamma)),
       .state_size = 2U,
       .change_root = authenticated::hash_empty(change_domain),
   };
   fixture.path = {{
       .child = authenticated::branch_side::left,
       .height = 1U,
       .subtree_size = 2U,
       .min_key = fixture.alpha.key,
       .max_key = fixture.gamma.key,
       .separator = fixture.gamma.key,
       .sibling = fixture.gamma,
   }};
   return fixture;
}

authenticated::point_proof authenticated_point_proof(const authenticated_point_fixture& fixture, std::string_view key) {
   return {
       .anchor = fixture.root,
       .key = authenticated_bytes(key),
       .terminal = fixture.alpha,
       .path = fixture.path,
   };
}

struct authenticated_ranked_fixture {
   std::string domain;
   std::string tree_domain;
   authenticated::proof_leaf a;
   authenticated::proof_leaf b;
   authenticated::proof_leaf c;
   authenticated::proof_leaf d;
   authenticated::proof_branch left;
   authenticated::proof_branch right;
   authenticated::root root;
};

authenticated_ranked_fixture make_authenticated_ranked_fixture() {
   auto fixture = authenticated_ranked_fixture{
       .domain = "forge.test.chain-api.authenticated-range.v3",
       .a = authenticated_leaf("a", "one"),
       .b = authenticated_leaf("b", "two"),
       .c = authenticated_leaf("c", "three"),
       .d = authenticated_leaf("d", "four"),
   };
   fixture.tree_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::state);
   fixture.left = {
       .height = 1U,
       .size = 2U,
       .min_key = fixture.a.key,
       .max_key = fixture.b.key,
       .separator = fixture.b.key,
       .left_hash = authenticated_leaf_hash(fixture.tree_domain, fixture.a),
       .right_hash = authenticated_leaf_hash(fixture.tree_domain, fixture.b),
   };
   fixture.right = {
       .height = 1U,
       .size = 2U,
       .min_key = fixture.c.key,
       .max_key = fixture.d.key,
       .separator = fixture.d.key,
       .left_hash = authenticated_leaf_hash(fixture.tree_domain, fixture.c),
       .right_hash = authenticated_leaf_hash(fixture.tree_domain, fixture.d),
   };
   fixture.root = {
       .version = 42U,
       .state_root = authenticated::hash_inner(fixture.tree_domain, 2U, 4U, fixture.a.key, fixture.d.key, fixture.c.key,
                                               authenticated_branch_hash(fixture.tree_domain, fixture.left),
                                               authenticated_branch_hash(fixture.tree_domain, fixture.right)),
       .state_size = 4U,
       .change_root = authenticated::hash_empty(
           authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::changes)),
   };
   return fixture;
}

authenticated::range_proof authenticated_ranked_proof(const authenticated_ranked_fixture& fixture,
                                                      authenticated::range_request request) {
   return {
       .anchor = fixture.root,
       .request = std::move(request),
       .nodes =
           {
               authenticated::range_inner{
                   .height = 2U,
                   .size = 4U,
                   .min_key = fixture.a.key,
                   .max_key = fixture.d.key,
                   .separator = fixture.c.key,
               },
               authenticated::range_inner{
                   .height = fixture.left.height,
                   .size = fixture.left.size,
                   .min_key = fixture.left.min_key,
                   .max_key = fixture.left.max_key,
                   .separator = fixture.left.separator,
               },
               fixture.a,
               fixture.b,
               authenticated::range_inner{
                   .height = fixture.right.height,
                   .size = fixture.right.size,
                   .min_key = fixture.right.min_key,
                   .max_key = fixture.right.max_key,
                   .separator = fixture.right.separator,
               },
               fixture.c,
               fixture.d,
           },
   };
}

struct authenticated_changes_fixture {
   std::string domain;
   authenticated::mutation erased;
   authenticated::mutation updated;
   authenticated::proof_leaf erased_leaf;
   authenticated::proof_leaf updated_leaf;
   authenticated::root root;
};

authenticated_changes_fixture make_authenticated_changes_fixture() {
   auto fixture = authenticated_changes_fixture{
       .domain = "forge.test.chain-api.authenticated-changes.v3",
       .erased = authenticated::mutation{.key = authenticated_bytes("alpha")},
       .updated =
           authenticated::mutation{
               .key = authenticated_bytes("beta"),
               .value = authenticated_bytes("updated"),
           },
   };
   fixture.erased_leaf = authenticated_change_leaf(fixture.erased);
   fixture.updated_leaf = authenticated_change_leaf(fixture.updated);
   const auto state_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::state);
   const auto change_domain = authenticated::canonical_tree_domain(fixture.domain, authenticated::proof_tree::changes);
   fixture.root = {
       .version = 43U,
       .state_root = authenticated::hash_empty(state_domain),
       .change_root = authenticated::hash_inner(change_domain, 1U, 2U, fixture.erased_leaf.key,
                                                fixture.updated_leaf.key, fixture.updated_leaf.key,
                                                authenticated_leaf_hash(change_domain, fixture.erased_leaf),
                                                authenticated_leaf_hash(change_domain, fixture.updated_leaf)),
       .change_count = 2U,
   };
   return fixture;
}

authenticated::range_proof authenticated_changes_proof(const authenticated_changes_fixture& fixture,
                                                       authenticated::range_request request) {
   return {
       .anchor = fixture.root,
       .tree = authenticated::proof_tree::changes,
       .request = std::move(request),
       .nodes =
           {
               authenticated::range_inner{
                   .height = 1U,
                   .size = 2U,
                   .min_key = fixture.erased_leaf.key,
                   .max_key = fixture.updated_leaf.key,
                   .separator = fixture.updated_leaf.key,
               },
               fixture.erased_leaf,
               fixture.updated_leaf,
           },
   };
}

const route& find_route(const std::vector<route>& routes, std::string_view name) {
   const auto result = std::ranges::find(routes, name, &route::method_name);
   BOOST_REQUIRE(result != routes.end());
   return *result;
}

void require_routes(const std::vector<route>& routes, method verb, std::initializer_list<std::string_view> names) {
   for (const auto name : names) {
      const auto& value = find_route(routes, name);
      BOOST_TEST(value.verb == verb);
      if (verb == method::get) {
         BOOST_TEST(static_cast<int>(value.cache) == static_cast<int>(cache_policy::no_store));
      }
   }
}

void require_audited_get_finality_anchor(const std::vector<route>& routes) {
   for (const auto& value : routes) {
      if (value.verb == method::get && value.target.find("audit={audit}") != std::string::npos) {
         BOOST_TEST(value.target.find("finality_from={finality_from}") != std::string::npos);
      }
   }
}

std::string openapi_verb(method value) {
   switch (value) {
   case method::delete_:
      return "delete";
   case method::get:
      return "get";
   case method::head:
      return "head";
   case method::options:
      return "options";
   case method::patch:
      return "patch";
   case method::post:
      return "post";
   case method::put:
      return "put";
   case method::unknown:
      return {};
   }
   return {};
}

std::string openapi_path(const route& value) {
   const auto query = value.target.find('?');
   return value.target.substr(0U, query);
}

template <typename Owner> std::vector<route> owner_routes() {
   return forge::api::http::traits<Owner>::routes();
}

template <typename Owner> forge::variant owner_openapi() {
   return forge::api::http::openapi<Owner>();
}

template <typename Owner> forge::api::core::descriptor owner_descriptor() {
   return Owner::describe();
}

struct owner_openapi_contract {
   std::string_view name;
   std::vector<route> (*routes)();
   forge::variant (*document)();
   forge::api::core::descriptor (*describe)();
};

} // namespace

BOOST_AUTO_TEST_CASE(table_key_codec_is_canonical_and_validates_index_contract) {
   using forge::chain::api::encode_table_key;
   using forge::chain::api::validate_table_index;
   using forge::chain::api::validate_table_key;
   using forge::chain::api::exceptions::invalid_request;
   namespace protocol = forge::chain::protocol;

   BOOST_TEST(encode_table_key(std::uint64_t{0x0102030405060708ULL}) ==
              (protocol::bytes{0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U}));
   BOOST_TEST(encode_table_key(-0.0) == encode_table_key(0.0));
   BOOST_TEST(encode_table_key(-1.0) < encode_table_key(0.0));
   BOOST_TEST(encode_table_key(0.0) < encode_table_key(1.0));
   BOOST_CHECK_THROW((void)encode_table_key(std::numeric_limits<double>::quiet_NaN()), invalid_request);

   auto negative_zero128 = std::array<std::uint8_t, 16>{};
   negative_zero128.front() = 0x80U;
   const auto positive_zero128 = std::array<std::uint8_t, 16>{};
   BOOST_TEST(encode_table_key(std::span<const std::uint8_t, 16>{negative_zero128}) ==
              encode_table_key(std::span<const std::uint8_t, 16>{positive_zero128}));
   auto nan128 = std::array<std::uint8_t, 16>{};
   nan128[0] = 0x7fU;
   nan128[1] = 0xffU;
   nan128.back() = 0x01U;
   BOOST_CHECK_THROW((void)encode_table_key(std::span<const std::uint8_t, 16>{nan128}), invalid_request);

   BOOST_CHECK_NO_THROW(validate_table_index({.kind = protocol::table_index_kind::primary, .position = 0U}));
   BOOST_CHECK_THROW(validate_table_index({.kind = protocol::table_index_kind::primary, .position = 1U}),
                     invalid_request);
   BOOST_CHECK_NO_THROW(validate_table_index({.kind = protocol::table_index_kind::secondary_u64, .position = 15U}));
   BOOST_CHECK_THROW(validate_table_index({.kind = protocol::table_index_kind::secondary_u64, .position = 16U}),
                     invalid_request);
   BOOST_CHECK_NO_THROW(validate_table_key(protocol::table_index_kind::secondary_u128, std::array<std::uint8_t, 16>{}));
   BOOST_CHECK_THROW(validate_table_key(protocol::table_index_kind::secondary_u128, std::array<std::uint8_t, 8>{}),
                     invalid_request);
}

BOOST_AUTO_TEST_CASE(chain_http_uses_resource_verbs) {
   const auto info = forge::api::http::traits<forge::chain::api::info>::routes();
   const auto blocks = forge::api::http::traits<forge::chain::api::block>::routes();
   const auto state = forge::api::http::traits<forge::chain::api::state>::routes();
   const auto transactions = forge::api::http::traits<forge::chain::api::transaction>::routes();
   const auto submissions = forge::api::http::traits<forge::chain::api::submission>::routes();
   const auto admin = forge::api::http::traits<forge::chain::api::admin>::routes();

   require_routes(info, method::get, {"get"});
   require_routes(blocks, method::get,
                  {"get_block", "get_header", "get_block_state", "get_canonical_range",
                   "get_activated_protocol_features", "get_consensus_parameters", "get_producers",
                   "get_producer_schedule", "get_finalizer_info"});
   require_routes(state, method::get,
                  {"get_account", "get_code", "get_table_rows", "get_table_scope", "get_currency_balance",
                   "get_currency_stats", "get_scheduled_transactions"});
   require_routes(state, method::post, {"get_point", "get_range", "get_changes", "get_accounts_by_authorizers"});
   require_routes(transactions, method::get, {"get_status", "await_transaction"});
   require_routes(transactions, method::post,
                  {"get_required_keys", "compute_transaction", "send_read_only_transaction"});
   require_routes(submissions, method::post, {"submit", "submit_batch"});
   require_routes(admin, method::get,
                  {"producer_status", "supported_protocol_features", "account_ram_corrections",
                   "unapplied_transactions", "snapshot_requests", "integrity_hash"});
   require_routes(admin, method::post, {"push_block", "create_snapshot", "prune", "schedule_snapshot"});
   require_routes(admin, method::put, {"configure_pause", "set_access_policy", "schedule_protocol_features"});
   require_routes(admin, method::patch, {"update_runtime_options", "update_greylist"});
   require_routes(admin, method::delete_, {"unschedule_snapshot"});

   require_audited_get_finality_anchor(info);
   require_audited_get_finality_anchor(blocks);
   require_audited_get_finality_anchor(state);
   require_audited_get_finality_anchor(transactions);
}

BOOST_AUTO_TEST_CASE(chain_api_limits_bound_canonical_request_and_response_bytes) {
   auto limits = forge::chain::protocol::service_limits{};
   auto request = forge::chain::protocol::state_point_request{.key = {1U, 2U, 3U}};
   limits.max_request_bytes = static_cast<std::uint32_t>(forge::raw::pack_size(request));
   BOOST_CHECK_NO_THROW(forge::chain::api::require_request_within_limits(request, limits));
   --limits.max_request_bytes;
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(request, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   auto response = forge::chain::protocol::state_point_response{.value = forge::chain::protocol::bytes{4U, 5U}};
   limits.max_response_bytes = static_cast<std::uint32_t>(forge::raw::pack_size(response));
   BOOST_CHECK_NO_THROW(forge::chain::api::require_response_within_limits(response, limits));
   --limits.max_response_bytes;
   BOOST_CHECK_THROW(forge::chain::api::require_response_within_limits(response, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   limits = forge::chain::protocol::service_limits{};
   auto table = forge::chain::protocol::table_rows_request{
       .index = {.kind = forge::chain::protocol::table_index_kind::secondary_u128, .position = 1U},
       .lower_bound = forge::chain::protocol::bytes(16U, 0U),
       .upper_bound = forge::chain::protocol::bytes(16U, 1U),
       .cursor = forge::chain::protocol::bytes{0xffU},
   };
   BOOST_CHECK_NO_THROW(forge::chain::api::require_request_within_limits(table, limits));
   table.limit = 0U;
   BOOST_CHECK_NO_THROW(forge::chain::api::require_request_within_limits(table, limits));
   table.limit = 10U;
   table.lower_bound = forge::chain::protocol::bytes(8U, 0U);
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(table, limits),
                     forge::chain::api::exceptions::invalid_request);

   auto range = forge::chain::protocol::state_range_request{.limit = limits.max_page_size + 1U};
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(range, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   table.lower_bound = forge::chain::protocol::bytes(16U, 0U);
   table.index.kind = static_cast<forge::chain::protocol::table_index_kind>(0xffU);
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(table, limits),
                     forge::chain::api::exceptions::invalid_request);

   auto range_request = forge::chain::protocol::state_range_request{.limit = 1U};
   auto range_response = forge::chain::protocol::state_range_response{
       .rows =
           {
               {.key = {1U}, .value = {1U}},
               {.key = {2U}, .value = {2U}},
           },
   };
   BOOST_CHECK_THROW(forge::chain::api::require_response_within_limits(range_response, range_request, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   auto changes = forge::chain::protocol::state_changes_request{
       .from_block = 10U,
       .to_block = 9U,
   };
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(changes, limits),
                     forge::chain::api::exceptions::invalid_request);

   auto waiting = forge::chain::protocol::transaction_await_request{.timeout_ms = limits.max_await_ms + 1U};
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(waiting, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   auto submission = forge::chain::protocol::transaction_submit_request{.timeout_ms = 0U};
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(submission, limits),
                     forge::chain::api::exceptions::invalid_request);
   submission.timeout_ms = limits.max_await_ms + 1U;
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(submission, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   auto batch = forge::chain::protocol::transaction_submit_batch_request{
       .transactions = {forge::chain::protocol::transaction_submit_request{.timeout_ms = 2'000U}},
       .timeout_ms = 1'000U,
   };
   BOOST_CHECK_NO_THROW(forge::chain::api::require_request_within_limits(batch, limits));
   batch.timeout_ms = limits.max_await_ms + 1U;
   BOOST_CHECK_THROW(forge::chain::api::require_request_within_limits(batch, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality", .payload = {1U, 2U, 3U}},
   };
   limits.max_response_bytes = std::numeric_limits<std::uint32_t>::max();
   limits.max_proof_bytes = static_cast<std::uint32_t>(forge::raw::pack_size(*response.audit) - 1U);
   BOOST_CHECK_THROW(forge::chain::api::require_response_within_limits(response, limits),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_enforces_owner_request_and_response_limits) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_page_size = 4U;
   auto service = std::make_shared<state_service>(forge::chain::protocol::state_range_response{
       .rows =
           {
               {.key = {1U}, .value = {1U}},
               {.key = {2U}, .value = {2U}},
           },
   });
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::state>(limits);
   const auto* method = forge::api::core::find_method(descriptor, "get_range");
   BOOST_REQUIRE(method != nullptr);

   const auto request = forge::chain::protocol::state_range_request{.limit = 1U};
   const auto request_bytes = forge::raw::pack(request);
   method->request_validator(request_bytes);
   const auto response_bytes = run(method->raw_invoker(service, request_bytes));
   BOOST_CHECK_THROW(method->response_validator(request_bytes, response_bytes),
                     forge::chain::api::exceptions::resource_exhausted);

   const auto oversized = forge::chain::protocol::state_range_request{.limit = limits.max_page_size + 1U};
   BOOST_CHECK_THROW(method->request_validator(forge::raw::pack(oversized)),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_rejects_malformed_and_unbounded_submission_deadlines) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_await_ms = 2'000U;
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::submission>(limits);
   const auto* submit = forge::api::core::find_method(descriptor, "submit");
   const auto* submit_batch = forge::api::core::find_method(descriptor, "submit_batch");
   BOOST_REQUIRE(submit != nullptr);
   BOOST_REQUIRE(submit_batch != nullptr);

   auto valid = forge::raw::pack(forge::chain::protocol::transaction_submit_request{.timeout_ms = 2'000U});
   BOOST_CHECK_NO_THROW(submit->request_validator(valid));
   valid.resize(valid.size() - sizeof(std::uint64_t));
   BOOST_CHECK_THROW(submit->request_validator(valid), forge::chain::api::exceptions::invalid_request);

   const auto over_limit = forge::raw::pack(forge::chain::protocol::transaction_submit_request{.timeout_ms = 2'001U});
   BOOST_CHECK_THROW(submit->request_validator(over_limit), forge::chain::api::exceptions::resource_exhausted);

   const auto bounded_batch = forge::raw::pack(forge::chain::protocol::transaction_submit_batch_request{
       .transactions = {forge::chain::protocol::transaction_submit_request{.timeout_ms = 1'500U}},
       .timeout_ms = 1'000U,
   });
   BOOST_CHECK_NO_THROW(submit_batch->request_validator(bounded_batch));
}

BOOST_AUTO_TEST_CASE(chain_api_producer_zero_limit_preserves_donor_continuation_semantics) {
   const auto limits = forge::chain::protocol::service_limits{};
   const auto request = forge::chain::protocol::producers_request{.limit = 0U};
   const auto response = forge::chain::protocol::producers_response{.next = "producer"};

   BOOST_CHECK_NO_THROW(forge::chain::api::require_request_within_limits(request, limits));
   BOOST_CHECK_NO_THROW(forge::chain::api::require_response_within_limits(response, request, limits));

   auto invalid = response;
   invalid.rows.emplace_back();
   BOOST_CHECK_THROW(forge::chain::api::require_response_within_limits(invalid, request, limits),
                     forge::chain::api::exceptions::resource_exhausted);

   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::block>(limits);
   const auto* method = forge::api::core::find_method(descriptor, "get_producers");
   BOOST_REQUIRE(method != nullptr);
   BOOST_CHECK_NO_THROW(method->request_validator(forge::raw::pack(request)));
   BOOST_CHECK_NO_THROW(method->response_validator(forge::raw::pack(request), forge::raw::pack(response)));
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_only_decodes_audited_response_types) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_container_elements = 2U;
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::transaction>(limits);
   const auto* required_keys = forge::api::core::find_method(descriptor, "get_required_keys");
   const auto* status = forge::api::core::find_method(descriptor, "get_status");
   BOOST_REQUIRE(required_keys != nullptr);
   BOOST_REQUIRE(status != nullptr);
   BOOST_TEST(!required_keys->has_response_trait<forge::chain::protocol::audited_response>());
   BOOST_TEST(status->has_response_trait<forge::chain::protocol::audited_response>());

   const auto plain_response = forge::raw::pack(std::vector<forge::chain::protocol::public_key>{});
   BOOST_CHECK_NO_THROW(required_keys->response_validator(
       forge::raw::pack(forge::chain::protocol::transaction_required_keys_request{}), plain_response));
   BOOST_CHECK_THROW(status->response_validator(forge::raw::pack(forge::chain::protocol::transaction_status_request{}),
                                                plain_response),
                     forge::chain::api::exceptions::unavailable);

   const auto oversized_keys = forge::raw::pack(std::vector<forge::chain::protocol::public_key>(3U));
   BOOST_CHECK_THROW(required_keys->response_validator(
                         forge::raw::pack(forge::chain::protocol::transaction_required_keys_request{}), oversized_keys),
                     forge::chain::api::exceptions::resource_exhausted);

   auto status_with_oversized_tail = forge::chain::protocol::transaction_status_response{};
   status_with_oversized_tail.trace = forge::chain::protocol::transaction_trace{};
   status_with_oversized_tail.trace->actions.resize(3U);
   BOOST_CHECK_THROW(status->response_validator(forge::raw::pack(forge::chain::protocol::transaction_status_request{}),
                                                forge::raw::pack(status_with_oversized_tail)),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_rejects_declared_collections_before_allocation) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_page_size = 1'024U;
   limits.max_transaction_batch_size = 2U;
   limits.max_container_elements = 4'096U;
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::submission>(limits);
   const auto* method = forge::api::core::find_method(descriptor, "submit_batch");
   BOOST_REQUIRE(method != nullptr);

   const auto over_transaction_limit = forge::api::core::bytes{0x03U};
   BOOST_CHECK_THROW(method->request_validator(over_transaction_limit),
                     forge::chain::api::exceptions::resource_exhausted);

   const auto declared_million_items = forge::api::core::bytes{0x80U, 0x80U, 0x40U};
   BOOST_CHECK_THROW(method->request_validator(declared_million_items),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_bounds_admin_pages_and_response_cardinality) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_page_size = 2U;
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::admin>(limits);
   const auto* ram = forge::api::core::find_method(descriptor, "account_ram_corrections");
   const auto* unapplied = forge::api::core::find_method(descriptor, "unapplied_transactions");
   BOOST_REQUIRE(ram != nullptr);
   BOOST_REQUIRE(unapplied != nullptr);

   const auto oversized_ram = forge::raw::pack(forge::chain::protocol::ram_corrections_request{.limit = 3U});
   BOOST_CHECK_THROW(ram->request_validator(oversized_ram), forge::chain::api::exceptions::resource_exhausted);
   const auto ram_request = forge::raw::pack(forge::chain::protocol::ram_corrections_request{.limit = 1U});
   const auto ram_response = forge::raw::pack(forge::chain::protocol::ram_corrections_response{
       .rows = {forge::variant{}, forge::variant{}},
   });
   BOOST_CHECK_THROW(ram->response_validator(ram_request, ram_response),
                     forge::chain::api::exceptions::resource_exhausted);

   const auto oversized_unapplied =
       forge::raw::pack(forge::chain::protocol::unapplied_transactions_request{.limit = 3U});
   BOOST_CHECK_THROW(unapplied->request_validator(oversized_unapplied),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_limited_descriptor_bounds_authorizer_inputs_before_complete_decode) {
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_state_batch_size = 2U;
   limits.max_container_elements = 4'096U;
   const auto descriptor = forge::chain::api::limited_descriptor<forge::chain::api::state>(limits);
   const auto* method = forge::api::core::find_method(descriptor, "get_accounts_by_authorizers");
   BOOST_REQUIRE(method != nullptr);

   const auto request = forge::chain::protocol::authorizers_request{
       .accounts = {forge::chain::protocol::permission_level{}, forge::chain::protocol::permission_level{}},
       .keys = {forge::chain::protocol::public_key{}},
       .limit = 1U,
   };
   BOOST_CHECK_THROW(method->request_validator(forge::raw::pack(request)),
                     forge::chain::api::exceptions::resource_exhausted);

   limits.max_state_batch_size = 0U;
   const auto zero_limit_descriptor = forge::chain::api::limited_descriptor<forge::chain::api::state>(limits);
   const auto* zero_limit_method = forge::api::core::find_method(zero_limit_descriptor, "get_accounts_by_authorizers");
   BOOST_REQUIRE(zero_limit_method != nullptr);
   const auto nonempty_request = forge::chain::protocol::authorizers_request{
       .accounts = {forge::chain::protocol::permission_level{}},
       .limit = 1U,
   };
   BOOST_CHECK_THROW(zero_limit_method->request_validator(forge::raw::pack(nonempty_request)),
                     forge::chain::api::exceptions::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(chain_api_transaction_batch_response_requires_exact_cardinality) {
   const auto limits = forge::chain::protocol::service_limits{};
   BOOST_CHECK_THROW(forge::chain::api::require_transaction_batch_response_within_limits({}, 1U, limits),
                     forge::chain::api::exceptions::unavailable);
}

BOOST_AUTO_TEST_CASE(chain_openapi_covers_every_owner_contract_route_and_schema) {
   const auto owners = std::array{
       owner_openapi_contract{"info", &owner_routes<forge::chain::api::info>, &owner_openapi<forge::chain::api::info>,
                              &owner_descriptor<forge::chain::api::info>},
       owner_openapi_contract{"block", &owner_routes<forge::chain::api::block>,
                              &owner_openapi<forge::chain::api::block>, &owner_descriptor<forge::chain::api::block>},
       owner_openapi_contract{"state", &owner_routes<forge::chain::api::state>,
                              &owner_openapi<forge::chain::api::state>, &owner_descriptor<forge::chain::api::state>},
       owner_openapi_contract{"transaction", &owner_routes<forge::chain::api::transaction>,
                              &owner_openapi<forge::chain::api::transaction>,
                              &owner_descriptor<forge::chain::api::transaction>},
       owner_openapi_contract{"admin", &owner_routes<forge::chain::api::admin>,
                              &owner_openapi<forge::chain::api::admin>, &owner_descriptor<forge::chain::api::admin>},
   };
   auto operation_ids = std::set<std::string>{};

   for (const auto& owner : owners) {
      const auto routes = owner.routes();
      const auto document = owner.document();
      const auto descriptor = owner.describe();
      BOOST_TEST(document["openapi"].as_string() == "3.1.0");

      const auto& paths = document["paths"].get_object();
      auto expected_paths = std::set<std::string>{};
      auto expected_operations = std::set<std::pair<std::string, std::string>>{};
      for (const auto& mapping : routes) {
         const auto* method_descriptor = forge::api::core::find_method(descriptor, mapping.method_name);
         BOOST_REQUIRE_MESSAGE(method_descriptor != nullptr,
                               owner.name << "." << mapping.method_name << " has a method descriptor");
         BOOST_REQUIRE_MESSAGE(!method_descriptor->errors.empty(),
                               owner.name << "." << mapping.method_name << " declares typed errors");
         const auto resource_identity =
             forge::api::core::exception_identity<forge::chain::api::exceptions::resource_exhausted>();
         BOOST_REQUIRE_MESSAGE(std::ranges::find(method_descriptor->errors, resource_identity,
                                                 &forge::api::core::error_descriptor::identity) !=
                                   method_descriptor->errors.end(),
                               owner.name << "." << mapping.method_name << " declares resource exhaustion");
         const auto path = openapi_path(mapping);
         const auto verb = openapi_verb(mapping.verb);
         BOOST_REQUIRE_MESSAGE(!verb.empty(), owner.name << "." << mapping.method_name << " has an HTTP verb");
         expected_paths.insert(path);
         BOOST_REQUIRE_MESSAGE(expected_operations.emplace(path, verb).second,
                               owner.name << "." << mapping.method_name << " has a unique path and verb");
      }

      BOOST_TEST(paths.size() == expected_paths.size());
      auto documented_operations = std::size_t{};
      for (const auto& path : paths) {
         documented_operations += path.value().get_object().size();
      }
      BOOST_TEST(documented_operations == expected_operations.size());

      for (const auto& mapping : routes) {
         const auto* method_descriptor = forge::api::core::find_method(descriptor, mapping.method_name);
         BOOST_REQUIRE_MESSAGE(method_descriptor != nullptr,
                               owner.name << "." << mapping.method_name << " has a method descriptor");
         const auto path = openapi_path(mapping);
         const auto verb = openapi_verb(mapping.verb);
         const auto path_entry = paths.find(path);
         BOOST_REQUIRE_MESSAGE(path_entry != paths.end(), owner.name << "." << mapping.method_name << " path exists");
         const auto& methods = path_entry->value().get_object();
         const auto method_entry = methods.find(verb);
         BOOST_REQUIRE_MESSAGE(method_entry != methods.end(),
                               owner.name << "." << mapping.method_name << " verb exists");
         const auto& operation = method_entry->value().get_object();

         const auto operation_id = operation["operationId"].as_string();
         BOOST_REQUIRE_MESSAGE(!operation_id.empty(), owner.name << "." << mapping.method_name << " has operationId");
         BOOST_CHECK_MESSAGE(operation_ids.insert(operation_id).second,
                             owner.name << "." << mapping.method_name << " operationId is unique");

         const auto& responses = operation["responses"].get_object();
         const auto success_status = std::to_string(static_cast<unsigned>(mapping.success_status));
         const auto success_entry = responses.find(success_status);
         BOOST_REQUIRE_MESSAGE(success_entry != responses.end(),
                               owner.name << "." << mapping.method_name << " has its success response");
         const auto& success = success_entry->value().get_object();
         BOOST_REQUIRE_MESSAGE(success.contains("content"),
                               owner.name << "." << mapping.method_name << " has success content");
         const auto& success_schema = success["content"]["application/json"]["schema"].get_object();
         BOOST_CHECK_MESSAGE(success_schema.size() != 0U,
                             owner.name << "." << mapping.method_name << " has a nonempty success schema");

         const auto error_entry = responses.find("default");
         BOOST_REQUIRE_MESSAGE(error_entry != responses.end(),
                               owner.name << "." << mapping.method_name << " has an error response");
         const auto& error = error_entry->value().get_object();
         BOOST_REQUIRE_MESSAGE(error.contains("content"),
                               owner.name << "." << mapping.method_name << " has error content");
         const auto& error_schema = error["content"]["application/json"]["schema"].get_object();
         BOOST_CHECK_MESSAGE(error_schema.size() != 0U,
                             owner.name << "." << mapping.method_name << " has a nonempty error schema");
         BOOST_TEST(error_schema["type"].as_string() == "object");
         const auto& error_properties = error_schema["properties"].get_object();
         for (const auto field :
              {"error", "message", "retryable", "status_code", "identity", "details_codec", "details"}) {
            BOOST_CHECK_MESSAGE(error_properties.contains(field),
                                owner.name << "." << mapping.method_name << " error envelope has " << field);
         }

         const auto& documented_errors = error_schema["x-forge-declared-errors"].get_array();
         BOOST_TEST(documented_errors.size() == method_descriptor->errors.size());
         for (const auto& declared : method_descriptor->errors) {
            const auto documented = std::ranges::find_if(documented_errors, [&](const forge::variant& candidate) {
               return candidate["name"].as_string() == declared.name;
            });
            BOOST_REQUIRE_MESSAGE(documented != documented_errors.end(),
                                  owner.name << "." << mapping.method_name << " documents " << declared.name);
            BOOST_TEST((*documented)["status_code"].as_uint64() == static_cast<std::uint64_t>(declared.status_code));
            BOOST_TEST((*documented)["retryable"].as_bool() == declared.retryable);
            BOOST_TEST((*documented)["identity"]["category"].as_string() == declared.identity.category);
            BOOST_TEST((*documented)["identity"]["code"].as_uint64() == declared.identity.code);
         }

         if (operation.contains("requestBody")) {
            const auto& request_schema = operation["requestBody"]["content"]["application/json"]["schema"].get_object();
            BOOST_CHECK_MESSAGE(request_schema.size() != 0U,
                                owner.name << "." << mapping.method_name << " has a nonempty request schema");
         }
      }
   }
}

BOOST_AUTO_TEST_CASE(chain_http_transaction_wait_uses_request_deadline) {
   const auto routes = forge::api::http::traits<forge::chain::api::transaction>::routes();
   const auto found = std::ranges::find(routes, std::string_view{"await_transaction"}, &route::method_name);
   BOOST_REQUIRE(found != routes.end());
   BOOST_REQUIRE(found->timeout_field.has_value());
   BOOST_TEST(*found->timeout_field == "timeout_ms");

   const auto options = forge::api::http::detail::request_options_for(
       *found, forge::chain::protocol::transaction_await_request{.timeout_ms = 300'000U});
   BOOST_TEST(options.timeout == std::chrono::milliseconds{305'000});
   BOOST_TEST(options.retry_idempotent);
   BOOST_TEST(options.max_retries == 1U);
}

BOOST_AUTO_TEST_CASE(chain_http_retry_policy_matches_idempotent_verbs) {
   for (const auto verb : {method::get, method::head, method::put, method::delete_, method::options}) {
      const auto options = forge::api::http::detail::request_options_for(route{.verb = verb}, 0);
      BOOST_TEST(options.retry_idempotent);
      BOOST_TEST(options.max_retries == 1U);
   }

   for (const auto verb : {method::post, method::patch}) {
      const auto options = forge::api::http::detail::request_options_for(route{.verb = verb}, 0);
      BOOST_TEST(!options.retry_idempotent);
      BOOST_TEST(options.max_retries == 0U);
   }
}

BOOST_AUTO_TEST_CASE(chain_http_submission_uses_request_and_batch_deadlines) {
   const auto routes = forge::api::http::traits<forge::chain::api::submission>::routes();
   const auto submit = std::ranges::find(routes, std::string_view{"submit"}, &route::method_name);
   const auto submit_batch = std::ranges::find(routes, std::string_view{"submit_batch"}, &route::method_name);
   BOOST_REQUIRE(submit != routes.end());
   BOOST_REQUIRE(submit_batch != routes.end());
   BOOST_REQUIRE(submit->timeout_field.has_value());
   BOOST_REQUIRE(submit_batch->timeout_field.has_value());
   BOOST_TEST(*submit->timeout_field == "timeout_ms");
   BOOST_TEST(*submit_batch->timeout_field == "timeout_ms");

   const auto submit_options = forge::api::http::detail::request_options_for(
       *submit, forge::chain::protocol::transaction_submit_request{.timeout_ms = 12'000U});
   BOOST_TEST(submit_options.timeout == std::chrono::milliseconds{17'000});
   BOOST_TEST(!submit_options.retry_idempotent);
   BOOST_TEST(submit_options.max_retries == 0U);

   const auto batch_options = forge::api::http::detail::request_options_for(
       *submit_batch, forge::chain::protocol::transaction_submit_batch_request{
                          .transactions = {forge::chain::protocol::transaction_submit_request{.timeout_ms = 1'000U}},
                          .timeout_ms = 20'000U,
                      });
   BOOST_TEST(batch_options.timeout == std::chrono::milliseconds{25'000});
   BOOST_TEST(!batch_options.retry_idempotent);
   BOOST_TEST(batch_options.max_retries == 0U);
}

BOOST_AUTO_TEST_CASE(chain_transaction_remote_deadline_restores_the_declared_exception) {
   const auto descriptor = forge::chain::api::transaction::describe();
   const auto* method = forge::api::core::find_method(descriptor, "await_transaction");
   BOOST_REQUIRE(method != nullptr);
   const auto identity = forge::api::core::exception_identity<forge::chain::api::exceptions::deadline_exceeded>();
   const auto declared = std::ranges::find(method->errors, identity, &forge::api::core::error_descriptor::identity);
   BOOST_REQUIRE(declared != method->errors.end());
   BOOST_CHECK(declared->status_code == forge::api::core::status::deadline_exceeded);
   BOOST_TEST(declared->retryable);
   const auto submission_descriptor = forge::chain::api::submission::describe();
   const auto* submit = forge::api::core::find_method(submission_descriptor, "submit");
   const auto* submit_batch = forge::api::core::find_method(submission_descriptor, "submit_batch");
   BOOST_REQUIRE(submit != nullptr);
   BOOST_REQUIRE(submit_batch != nullptr);
   BOOST_CHECK(std::ranges::find(submit->errors, identity, &forge::api::core::error_descriptor::identity) !=
               submit->errors.end());
   BOOST_CHECK(std::ranges::find(submit_batch->errors, identity, &forge::api::core::error_descriptor::identity) !=
               submit_batch->errors.end());
   const auto mutation_identities =
       std::array{forge::api::core::exception_identity<forge::chain::api::exceptions::conflict>(),
                  forge::api::core::exception_identity<forge::chain::api::exceptions::admission_rejected>()};
   for (const auto& mutation_identity : mutation_identities) {
      BOOST_CHECK(std::ranges::find(submit->errors, mutation_identity, &forge::api::core::error_descriptor::identity) !=
                  submit->errors.end());
   }

   auto invoker = std::make_shared<deadline_remote_invoker>();
   auto remote = forge::api::core::proxy<forge::chain::api::transaction>{invoker};
   BOOST_CHECK_THROW(run(remote.await_transaction({.timeout_ms = 1U})),
                     forge::chain::api::exceptions::deadline_exceeded);
   BOOST_TEST(invoker->calls == 1U);
}

BOOST_AUTO_TEST_CASE(chain_admin_declares_mutation_errors_only_for_mutating_methods) {
   const auto descriptor = forge::chain::api::admin::describe();
   const auto identity = forge::api::core::exception_identity<forge::chain::api::exceptions::conflict>();
   const auto* push = forge::api::core::find_method(descriptor, "push_block");
   const auto* status = forge::api::core::find_method(descriptor, "producer_status");
   BOOST_REQUIRE(push != nullptr);
   BOOST_REQUIRE(status != nullptr);
   BOOST_CHECK(std::ranges::find(push->errors, identity, &forge::api::core::error_descriptor::identity) !=
               push->errors.end());
   BOOST_CHECK(std::ranges::find(status->errors, identity, &forge::api::core::error_descriptor::identity) ==
               status->errors.end());
}

BOOST_AUTO_TEST_CASE(chain_http_omits_an_unspecified_anchor) {
   const auto routes = forge::api::http::traits<forge::chain::api::block>::routes();
   const auto& route = find_route(routes, "get_consensus_parameters");
   const auto target = forge::api::http::detail::render_route_target(
       route, forge::chain::protocol::anchored_request{.anchor = std::nullopt,
                                                       .audit = forge::chain::protocol::audit_mode::required});

   BOOST_TEST(target == "/v1/chain/blocks/consensus-parameters?audit=required");
}

BOOST_AUTO_TEST_CASE(chain_http_carries_the_verified_finality_checkpoint) {
   const auto routes = forge::api::http::traits<forge::chain::api::info>::routes();
   const auto& route = find_route(routes, "get");
   auto checkpoint = forge::chain::protocol::block_id{};
   checkpoint._hash[0] = 0x42U;
   const auto target =
       forge::api::http::detail::render_route_target(route, forge::chain::protocol::anchored_request{
                                                                .finality_from = checkpoint,
                                                                .audit = forge::chain::protocol::audit_mode::required,
                                                            });

   BOOST_TEST(target.find("finality_from=") != std::string::npos);
   BOOST_TEST(target.find("audit=required") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(chain_table_scope_http_carries_the_opaque_cursor) {
   const auto routes = forge::api::http::traits<forge::chain::api::state>::routes();
   const auto& route = find_route(routes, "get_table_scope");
   const auto target = forge::api::http::detail::render_route_target(
       route, forge::chain::protocol::table_scope_request{
                  .code = forge::chain::protocol::account_name{"eosio.token"},
                  .table = forge::chain::protocol::name{"accounts"},
                  .cursor = forge::chain::protocol::bytes{0x00U, 0x2fU, 0xffU},
              });

   BOOST_TEST(route.target.find("&cursor={cursor}&") != std::string::npos);
   BOOST_TEST(target.find("cursor=%5B0%2C47%2C255%5D") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(chain_table_rows_http_carries_the_secondary_index) {
   const auto routes = forge::api::http::traits<forge::chain::api::state>::routes();
   const auto& route = find_route(routes, "get_table_rows");
   const auto index = forge::chain::protocol::table_index{
       .kind = forge::chain::protocol::table_index_kind::secondary_u128,
       .position = 2U,
   };
   const auto target = forge::api::http::detail::render_route_target(
       route, forge::chain::protocol::table_rows_request{
                  .code = forge::chain::protocol::account_name{"storlane"},
                  .scope = forge::chain::protocol::name{"storlane"},
                  .table = forge::chain::protocol::name{"revgeometry"},
                  .index = index,
              });

   BOOST_TEST(target.find("index=secondary-u128%3A2") != std::string::npos);
   BOOST_CHECK(forge::schema::parse_scalar_text<forge::chain::protocol::table_index>("secondary-u128:2") == index);
   BOOST_CHECK_THROW(
       static_cast<void>(forge::schema::parse_scalar_text<forge::chain::protocol::table_index>("secondary-u128")),
       forge::schema::exceptions::invalid_value);
}

BOOST_AUTO_TEST_CASE(chain_currency_stats_http_formats_the_symbol_code_path) {
   const auto routes = forge::api::http::traits<forge::chain::api::state>::routes();
   const auto& route = find_route(routes, "get_currency_stats");
   const auto target = forge::api::http::detail::render_route_target(
       route, forge::chain::protocol::currency_stats_request{
                  .code = forge::chain::protocol::account_name{"eosio.token"},
                  .symbol = forge::chain::protocol::symbol_code{"SYS"},
              });

   BOOST_TEST(target.starts_with("/v1/chain/state/currencies/eosio.token/stats/SYS?"));
   BOOST_CHECK(forge::schema::parse_scalar_text<forge::chain::protocol::symbol_code>("SYS") ==
               forge::chain::protocol::symbol_code{"SYS"});
}

BOOST_AUTO_TEST_CASE(chain_openapi_uses_canonical_public_key_json_shape) {
   const auto document = forge::api::http::openapi<forge::chain::api::transaction>();
   const auto& schema = document["paths"]["/v1/chain/transactions/required-keys"]["post"]["responses"]["200"]["content"]
                                ["application/json"]["schema"]["items"];

   BOOST_TEST(schema["type"].as_string() == "string");
   BOOST_TEST(schema["format"].as_string() == "forge-public-key");
}

BOOST_AUTO_TEST_CASE(chain_openapi_omits_body_for_query_only_admin_action) {
   const auto document = forge::api::http::openapi<forge::chain::api::admin>();
   const auto& operation = document["paths"]["/v1/chain/admin/snapshots"]["post"];

   BOOST_TEST(!operation.get_object().contains("requestBody"));
   const auto& parameters = operation["parameters"].get_array();
   const auto name = std::ranges::find_if(
       parameters, [](const forge::variant& value) { return value["name"].as_string() == "name"; });
   BOOST_REQUIRE(name != parameters.end());
   BOOST_TEST((*name)["in"].as_string() == "query");
}

BOOST_AUTO_TEST_CASE(chain_table_scope_openapi_exposes_json_bytes_cursor_and_next) {
   const auto document = forge::api::http::openapi<forge::chain::api::state>();
   const auto& operation = document["paths"]["/v1/chain/state/tables/{code}/scopes"]["get"];
   const auto& parameters = operation["parameters"].get_array();
   const auto cursor = std::ranges::find_if(
       parameters, [](const forge::variant& value) { return value["name"].as_string() == "cursor"; });
   BOOST_REQUIRE(cursor != parameters.end());
   BOOST_TEST((*cursor)["required"].as_bool() == false);
   BOOST_TEST(!cursor->get_object().contains("schema"));
   const auto& cursor_schema = (*cursor)["content"]["application/json"]["schema"];
   BOOST_TEST(cursor_schema["anyOf"][std::size_t{0}]["type"].as_string() == "array");
   BOOST_TEST(cursor_schema["anyOf"][std::size_t{0}]["items"]["type"].as_string() == "integer");

   const auto& properties =
       operation["responses"]["200"]["content"]["application/json"]["schema"]["properties"].get_object();
   BOOST_TEST(properties.contains("next"));
   BOOST_TEST(!properties.contains("more"));
   BOOST_TEST(!properties.contains("next_key"));
   BOOST_TEST(properties["next"]["anyOf"][std::size_t{0}]["type"].as_string() == "array");
}

BOOST_AUTO_TEST_CASE(verified_block_response_is_bound_to_the_requested_identity) {
   auto response = forge::chain::protocol::block_response{};
   response.id = response.block.calculate_id();
   response.num = response.block.calculate_block_num();
   response.canonical = true;
   response.context = forge::chain::protocol::response_context{
       .chain = {},
       .head = response.id,
       .finalized = response.id,
       .anchor =
           forge::chain::protocol::state_anchor{
               .chain = {},
               .block = response.id,
               .block_num = response.num,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality =
           forge::chain::protocol::proof_blob{
               .scheme = "test.finality",
               .version = 1,
           },
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::block>(std::make_shared<block_service>(response));
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
       }},
       std::make_shared<accepting_audit_verifier>(),
   };

   auto other = response.id;
   ++other._hash[1];
   BOOST_CHECK_THROW(run(client.get_block({.id = other})), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_THROW(run(client.get_block({.num = response.num + 1U})),
                     forge::chain::api::exceptions::invalid_finality);
   const auto verified = run(client.get_block({.id = response.id, .num = response.num}));
   BOOST_TEST(verified.id == response.id);
}

BOOST_AUTO_TEST_CASE(verified_block_rejects_transaction_receipts_not_committed_by_its_header) {
   auto response = forge::chain::protocol::block_response{};
   auto receipt = forge::chain::protocol::transaction_receipt{};
   receipt.status = forge::chain::protocol::transaction_receipt::status::executed;
   receipt.cpu_usage_us = 7U;
   auto receipt_id = forge::chain::protocol::transaction_id{};
   receipt_id._hash[0] = 17U;
   receipt.trx = receipt_id;
   response.block.transactions.push_back(receipt);
   response.block.transaction_mroot = forge::chain::protocol::calculate_transaction_mroot(response.block.transactions);
   response.id = response.block.calculate_id();
   response.num = response.block.calculate_block_num();
   response.canonical = true;
   response.context = forge::chain::protocol::response_context{
       .head = response.id,
       .finalized = response.id,
       .anchor =
           forge::chain::protocol::state_anchor{
               .block = response.id,
               .block_num = response.num,
               .transaction_root = response.block.transaction_mroot,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   auto mutated = response;
   ++mutated.block.transactions.front().cpu_usage_us;
   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::block>(std::make_shared<block_service>(std::move(mutated)));
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
       }},
       std::make_shared<accepting_audit_verifier>(),
   };

   BOOST_CHECK_THROW(run(client.get_block({.id = response.id, .num = response.num})),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_header_is_bound_to_its_request_and_finalized_anchor) {
   auto response = forge::chain::protocol::block_header_response{};
   response.header.transaction_mroot._hash[0] = 13U;
   response.id = response.header.calculate_id();
   response.num = response.header.calculate_block_num();
   response.canonical = true;
   response.context.anchor = forge::chain::protocol::state_anchor{
       .block = response.id,
       .block_num = response.num,
       .transaction_root = response.header.transaction_mroot,
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   const auto verify = [&](forge::chain::protocol::block_header_response candidate) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::block>(std::make_shared<block_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.get_header({.id = response.id, .num = response.num}));
   };

   BOOST_TEST(verify(response).id == response.id);

   auto mutated = response;
   ++mutated.header.transaction_mroot._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(mutated))), forge::chain::api::exceptions::invalid_finality);

   auto non_canonical = response;
   non_canonical.canonical = false;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(non_canonical))),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_raw_state_queries_delegate_content_proofs) {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 21U;
   anchor.block_num = 21U;
   const auto audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   {
      auto response = forge::chain::protocol::state_point_response{};
      response.context.anchor = anchor;
      response.audit = audit;
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };

      static_cast<void>(run(client.get_point({.key = {1U}, .anchor = anchor.block})));
      BOOST_TEST(verifier->state_point_verifications == 1U);
   }

   {
      auto response = forge::chain::protocol::state_range_response{};
      response.context.anchor = anchor;
      response.audit = audit;
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };

      static_cast<void>(run(client.get_range({.anchor = anchor.block})));
      BOOST_TEST(verifier->state_range_verifications == 1U);
   }
}

BOOST_AUTO_TEST_CASE(verified_client_uses_preferred_finality_anchor_without_overwriting_an_explicit_anchor) {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 21U;
   anchor.block_num = 21U;
   auto response = forge::chain::protocol::state_point_response{};
   response.context.anchor = anchor;
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   auto services = forge::api::core::registry{};
   auto service = std::make_shared<state_service>(response);
   services.install<forge::chain::api::state>(service);
   auto verifier = std::make_shared<accepting_audit_verifier>();
   auto preferred = forge::chain::protocol::block_id{};
   preferred._hash[0] = 8U;
   verifier->preferred_anchor = preferred;
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
       }},
       verifier,
   };

   static_cast<void>(run(client.get_point({.key = {1U}, .anchor = anchor.block})));
   BOOST_REQUIRE(service->last_point_request.has_value());
   BOOST_REQUIRE(service->last_point_request->finality_from.has_value());
   BOOST_TEST(*service->last_point_request->finality_from == preferred);

   auto explicit_anchor = forge::chain::protocol::block_id{};
   explicit_anchor._hash[0] = 13U;
   static_cast<void>(run(client.get_point({
       .key = {1U},
       .anchor = anchor.block,
       .finality_from = explicit_anchor,
   })));
   BOOST_REQUIRE(service->last_point_request.has_value());
   BOOST_REQUIRE(service->last_point_request->finality_from.has_value());
   BOOST_TEST(*service->last_point_request->finality_from == explicit_anchor);
}

BOOST_AUTO_TEST_CASE(verified_client_translates_extension_failures_to_typed_errors) {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 21U;
   anchor.block_num = 21U;
   auto response = forge::chain::protocol::state_point_response{};
   response.context.anchor = anchor;
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   const auto make_client = [&](const std::shared_ptr<accepting_audit_verifier>& verifier) {
      auto services = std::make_shared<forge::api::core::registry>();
      services->install<forge::chain::api::state>(std::make_shared<state_service>(response));
      return std::pair{
          forge::chain::api::verified_client{
              forge::chain::api::raw_client{forge::chain::api::service_handles{
                  .state_queries = services->get<forge::chain::api::state>(forge::chain::api::state::ref()),
              }},
              verifier,
          },
          std::move(services),
      };
   };

   auto context_verifier = std::make_shared<accepting_audit_verifier>();
   context_verifier->throw_standard_context = true;
   auto context_client = make_client(context_verifier);
   BOOST_CHECK_THROW(static_cast<void>(run(context_client.first.get_point({.key = {1U}, .anchor = anchor.block}))),
                     forge::chain::api::exceptions::invalid_state_proof);

   auto point_verifier = std::make_shared<accepting_audit_verifier>();
   point_verifier->throw_nonstandard_state_point = true;
   auto point_client = make_client(point_verifier);
   BOOST_CHECK_THROW(static_cast<void>(run(point_client.first.get_point({.key = {1U}, .anchor = anchor.block}))),
                     forge::chain::api::exceptions::invalid_state_proof);

   auto anchor_verifier = std::make_shared<accepting_audit_verifier>();
   anchor_verifier->throw_standard_preferred_anchor = true;
   auto anchor_client = make_client(anchor_verifier);
   BOOST_CHECK_THROW(static_cast<void>(run(anchor_client.first.get_point({.key = {1U}, .anchor = anchor.block}))),
                     forge::chain::api::exceptions::anchor_unavailable);
}

BOOST_AUTO_TEST_CASE(verified_client_translates_service_failures_and_cancellation) {
   auto services = forge::api::core::registry{};
   auto service = std::make_shared<state_service>(forge::chain::protocol::state_point_response{});
   services.install<forge::chain::api::state>(service);
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
       }},
       std::make_shared<accepting_audit_verifier>(),
   };

   service->point_failure = state_service::failure::standard;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::unavailable);
   service->point_failure = state_service::failure::nonstandard;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::unavailable);
   service->point_failure = state_service::failure::canceled;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))), forge::asio::exceptions::canceled);
   service->point_failure = state_service::failure::timed_out;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::deadline_exceeded);
   service->point_failure = state_service::failure::api_canceled;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))), forge::asio::exceptions::canceled);
   service->point_failure = state_service::failure::api_deadline;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::deadline_exceeded);
   service->point_failure = state_service::failure::foreign_forge;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::unavailable);
   service->point_failure = state_service::failure::chain_api;
   BOOST_CHECK_THROW(static_cast<void>(run(client.get_point({.key = {1U}}))),
                     forge::chain::api::exceptions::invalid_request);
}

BOOST_AUTO_TEST_CASE(verified_info_rejects_payload_identity_inconsistent_with_audited_context) {
   auto chain = forge::chain::protocol::chain_id{};
   chain._hash[0] = 1U;
   const auto finalized_header = forge::chain::protocol::signed_block_header{};
   const auto finalized = finalized_header.calculate_id();
   auto head_header = forge::chain::protocol::signed_block_header{};
   head_header.previous = finalized;
   const auto head = head_header.calculate_id();

   auto response = forge::chain::protocol::info_response{};
   response.chain = chain;
   response.head = head;
   response.head_num = head_header.calculate_block_num();
   response.finalized = finalized;
   response.finalized_num = finalized_header.calculate_block_num();
   response.context = forge::chain::protocol::response_context{
       .chain = chain,
       .head = head,
       .finalized = finalized,
       .anchor =
           forge::chain::protocol::state_anchor{
               .chain = chain,
               .block = finalized,
               .block_num = response.finalized_num,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   const auto verify = [](forge::chain::protocol::info_response candidate,
                          forge::chain::protocol::anchored_request request = {}) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::info>(std::make_shared<info_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .information = services.get<forge::chain::api::info>(forge::chain::api::info::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.get_info(std::move(request)));
   };

   BOOST_TEST(verify(response).chain == chain);

   auto wrong_anchor = finalized;
   ++wrong_anchor._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(response, {.anchor = wrong_anchor})),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_chain = response;
   ++wrong_chain.chain._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_chain))), forge::chain::api::exceptions::wrong_chain);

   auto wrong_head = response;
   ++wrong_head.head._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_head))), forge::chain::api::exceptions::invalid_finality);

   auto wrong_head_num = response;
   ++wrong_head_num.head_num;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_head_num))),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_finalized = response;
   ++wrong_finalized.finalized._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_finalized))),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_finalized_num = response;
   ++wrong_finalized_num.finalized_num;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_finalized_num))),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_await_transaction_enforces_requested_finality) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 29U;
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 42U;
   anchor.block_num = 42U;

   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::included;
   response.context.anchor = anchor;
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   const auto await = [&](forge::chain::protocol::transaction_status_response candidate,
                          forge::chain::protocol::transaction_lifecycle desired) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.await_transaction({.id = id, .desired = desired}));
   };

   BOOST_CHECK_THROW(static_cast<void>(await(response, forge::chain::protocol::transaction_lifecycle::finalized)),
                     forge::chain::api::exceptions::invalid_transaction_proof);

   auto wrong_head = response;
   ++wrong_head.head._hash[0];
   BOOST_CHECK_THROW(
       static_cast<void>(await(std::move(wrong_head), forge::chain::protocol::transaction_lifecycle::included)),
       forge::chain::api::exceptions::invalid_finality);

   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   BOOST_TEST(static_cast<unsigned>(await(response, forge::chain::protocol::transaction_lifecycle::finalized).state) ==
              static_cast<unsigned>(forge::chain::protocol::transaction_lifecycle::finalized));
   BOOST_TEST(static_cast<unsigned>(await(response, forge::chain::protocol::transaction_lifecycle::included).state) ==
              static_cast<unsigned>(forge::chain::protocol::transaction_lifecycle::finalized));
}

BOOST_AUTO_TEST_CASE(verified_transaction_status_delegates_the_inclusion_proof) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 31U;
   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   response.context.anchor = forge::chain::protocol::state_anchor{.block_num = 7U};
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(response)));
   auto verifier = std::make_shared<accepting_audit_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
       }},
       verifier,
   };

   static_cast<void>(run(client.get_transaction_status({.id = id})));
   BOOST_TEST(verifier->transaction_verifications == 1U);
}

BOOST_AUTO_TEST_CASE(submission_client_binds_acknowledgements_to_local_transaction_ids) {
   auto first = forge::chain::protocol::transaction_submit_request{};
   auto first_transaction = forge::chain::protocol::signed_transaction{};
   first_transaction.expiration = forge::chain::protocol::time_point_sec{1U};
   first.transaction = forge::chain::protocol::packed_transaction{std::move(first_transaction)};
   auto second = forge::chain::protocol::transaction_submit_request{};
   auto second_transaction = forge::chain::protocol::signed_transaction{};
   second_transaction.expiration = forge::chain::protocol::time_point_sec{2U};
   second.transaction = forge::chain::protocol::packed_transaction{std::move(second_transaction)};
   const auto first_id = first.transaction.id();
   const auto second_id = second.transaction.id();
   const auto batch = [&] {
      return forge::chain::protocol::transaction_submit_batch_request{.transactions = {first, second}};
   };

   const auto make_client = [&](std::vector<forge::chain::protocol::transaction_submit_response> responses) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::submission>(std::make_shared<submission_service>(std::move(responses)));
      return forge::chain::api::submission_client{
          services.get<forge::chain::api::submission>(forge::chain::api::submission::ref()),
      };
   };

   {
      auto client = make_client({forge::chain::protocol::transaction_submit_response{.id = first_id}});
      BOOST_TEST(run(client.submit(first)).id == first_id);
   }
   {
      auto client = make_client({forge::chain::protocol::transaction_submit_response{.id = second_id}});
      BOOST_CHECK_THROW(static_cast<void>(run(client.submit(first))),
                        forge::chain::api::exceptions::invalid_transaction_proof);
   }
   {
      auto response = forge::chain::protocol::transaction_submit_response{.id = first_id};
      response.trace = forge::chain::protocol::transaction_trace{.id = second_id};
      auto client = make_client({std::move(response)});
      BOOST_CHECK_THROW(static_cast<void>(run(client.submit(first))),
                        forge::chain::api::exceptions::invalid_transaction_proof);
   }
   {
      auto client = make_client({forge::chain::protocol::transaction_submit_response{.id = first_id},
                                 forge::chain::protocol::transaction_submit_response{.id = second_id}});
      const auto responses = run(client.submit_batch(batch()));
      BOOST_TEST(responses.size() == 2U);
      BOOST_TEST(responses[0].id == first_id);
      BOOST_TEST(responses[1].id == second_id);
   }
   {
      auto client = make_client({forge::chain::protocol::transaction_submit_response{.id = second_id},
                                 forge::chain::protocol::transaction_submit_response{.id = first_id}});
      BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch(batch()))),
                        forge::chain::api::exceptions::invalid_transaction_proof);
   }
   {
      auto first_response = forge::chain::protocol::transaction_submit_response{.id = first_id};
      auto second_response = forge::chain::protocol::transaction_submit_response{.id = second_id};
      second_response.trace = forge::chain::protocol::transaction_trace{.id = first_id};
      auto client = make_client({std::move(first_response), std::move(second_response)});
      BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch(batch()))),
                        forge::chain::api::exceptions::invalid_transaction_proof);
   }
   {
      auto client = make_client({forge::chain::protocol::transaction_submit_response{.id = first_id}});
      BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch(batch()))),
                        forge::chain::api::exceptions::unavailable);
   }
}

BOOST_AUTO_TEST_CASE(submission_client_fails_typed_without_a_transport) {
   auto client = forge::chain::api::submission_client{{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit({}))), forge::chain::api::exceptions::unavailable);
}

BOOST_AUTO_TEST_CASE(submission_client_enforces_local_limits_and_translates_service_failures) {
   auto services = forge::api::core::registry{};
   auto service =
       std::make_shared<submission_service>(std::vector<forge::chain::protocol::transaction_submit_response>{});
   services.install<forge::chain::api::submission>(service);
   auto limits = forge::chain::protocol::service_limits{};
   limits.max_transaction_batch_size = 1U;
   auto client = forge::chain::api::submission_client{
       services.get<forge::chain::api::submission>(forge::chain::api::submission::ref()), limits};

   BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch({}))), forge::chain::api::exceptions::invalid_request);
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch({.transactions = {{}, {}}}))),
                     forge::chain::api::exceptions::resource_exhausted);
   service->throw_standard = true;
   auto request = forge::chain::protocol::transaction_submit_request{};
   request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit(std::move(request)))),
                     forge::chain::api::exceptions::unavailable);
   service->throw_standard = false;
   service->throw_forge = true;
   request = forge::chain::protocol::transaction_submit_request{};
   request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit(std::move(request)))),
                     forge::chain::api::exceptions::unavailable);
   service->throw_forge = false;
   service->throw_timed_out = true;
   request = forge::chain::protocol::transaction_submit_request{};
   request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit(std::move(request)))),
                     forge::chain::api::exceptions::deadline_exceeded);
   service->throw_timed_out = false;
   service->throw_api_canceled = true;
   request = forge::chain::protocol::transaction_submit_request{};
   request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit(std::move(request)))), forge::asio::exceptions::canceled);
   auto batch_request = forge::chain::protocol::transaction_submit_request{};
   batch_request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch({.transactions = {std::move(batch_request)}}))),
                     forge::asio::exceptions::canceled);
   service->throw_api_canceled = false;
   service->throw_api_deadline = true;
   request = forge::chain::protocol::transaction_submit_request{};
   request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit(std::move(request)))),
                     forge::chain::api::exceptions::deadline_exceeded);
   batch_request = forge::chain::protocol::transaction_submit_request{};
   batch_request.transaction = forge::chain::protocol::packed_transaction{forge::chain::protocol::signed_transaction{}};
   BOOST_CHECK_THROW(static_cast<void>(run(client.submit_batch({.transactions = {std::move(batch_request)}}))),
                     forge::chain::api::exceptions::deadline_exceeded);
}

BOOST_AUTO_TEST_CASE(verified_transaction_status_rejects_an_unauthenticated_execution_trace) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 32U;
   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   response.trace = forge::chain::protocol::transaction_trace{.id = id};
   response.context.anchor = forge::chain::protocol::state_anchor{.block_num = 7U};
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(response)));
   auto verifier = std::make_shared<accepting_audit_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
       }},
       verifier,
   };

   BOOST_CHECK_THROW(static_cast<void>(run(client.get_transaction_status({.id = id}))),
                     forge::chain::api::exceptions::invalid_transaction_proof);
   BOOST_TEST(verifier->transaction_verifications == 0U);
}

BOOST_AUTO_TEST_CASE(verified_transaction_translates_verifier_failures_to_typed_errors) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 33U;
   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::included;
   response.context.anchor = forge::chain::protocol::state_anchor{};
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(response)));
   auto verifier = std::make_shared<accepting_audit_verifier>();
   verifier->throw_standard_transaction = true;
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
       }},
       verifier,
   };

   BOOST_CHECK_THROW(static_cast<void>(run(client.get_transaction_status({.id = id}))),
                     forge::chain::api::exceptions::invalid_transaction_proof);
}

BOOST_AUTO_TEST_CASE(verified_composite_response_delegates_product_projection_and_authenticated_sources) {
   auto anchor = make_finality_anchor();
   auto response = forge::chain::protocol::account_response{};
   response.account = forge::chain::protocol::account_name{"alice"};
   response.context = forge::chain::protocol::response_context{
       .chain = anchor.chain,
       .head = anchor.block,
       .finalized = anchor.block,
       .anchor = anchor,
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
   auto audit = std::make_shared<accepting_audit_verifier>();
   audit->point_value = forge::chain::protocol::bytes{1U, 2U};
   auto projections = std::make_shared<account_projection_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
       }},
       audit,
       projections,
   };

   const auto result =
       run(client.get_account({.account = forge::chain::protocol::account_name{"alice"}, .anchor = anchor.block}));

   BOOST_CHECK(result.account == forge::chain::protocol::account_name{"alice"});
   BOOST_TEST(projections->verifications == 1U);
   BOOST_TEST(audit->state_point_verifications == 1U);
}

BOOST_AUTO_TEST_CASE(verified_composite_response_translates_projection_failures_to_typed_errors) {
   auto anchor = make_finality_anchor();
   auto response = forge::chain::protocol::account_response{};
   response.context.anchor = anchor;
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   const auto verify = [&](bool nonstandard) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(response));
      auto projections = std::make_shared<throwing_account_projection_verifier>();
      projections->nonstandard = nonstandard;
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
          std::move(projections),
      };
      static_cast<void>(run(client.get_account({.anchor = anchor.block})));
   };

   BOOST_CHECK_THROW(verify(false), forge::chain::api::exceptions::invalid_state_proof);
   BOOST_CHECK_THROW(verify(true), forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(verified_client_fails_closed_for_methods_without_content_witnesses) {
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{}},
       std::make_shared<accepting_audit_verifier>(),
   };

   BOOST_CHECK_THROW(run(client.get_block_state(forge::chain::protocol::block_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_canonical_range(forge::chain::protocol::block_range_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_activated_protocol_features(forge::chain::protocol::protocol_features_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_consensus_parameters(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_producers(forge::chain::protocol::producers_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_producer_schedule(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_finalizer_info(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);

   BOOST_CHECK_THROW(run(client.get_account(forge::chain::protocol::account_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_code(forge::chain::protocol::code_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_table_rows(forge::chain::protocol::table_rows_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_table_scope(forge::chain::protocol::table_scope_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_currency_balance(forge::chain::protocol::currency_balance_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_currency_stats(forge::chain::protocol::currency_stats_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_scheduled_transactions(forge::chain::protocol::scheduled_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_accounts_by_authorizers(forge::chain::protocol::authorizers_request{})),
                     forge::chain::api::exceptions::audit_not_supported);

   BOOST_CHECK_THROW(run(client.get_required_keys(forge::chain::protocol::transaction_required_keys_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.compute_transaction(forge::chain::protocol::transaction_read_only_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.send_read_only_transaction(forge::chain::protocol::transaction_read_only_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
}

BOOST_AUTO_TEST_CASE(verified_changes_cover_the_requested_interval_and_terminal_anchor) {
   auto first = forge::chain::protocol::state_anchor{.block_num = 11U};
   first.block._hash[0] = 11U;
   auto second = forge::chain::protocol::state_anchor{.block_num = 12U};
   second.block._hash[0] = 12U;

   const auto make_response = [&] {
      auto response = forge::chain::protocol::state_changes_response{};
      response.context.anchor = second;
      response.blocks = {
          {.anchor = first, .ranges = {{.range = {}}}},
          {.anchor = second, .ranges = {{.range = {}}}},
      };
      response.audit = forge::chain::protocol::audit_bundle{};
      response.audit->finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
      response.audit->ancestry = forge::chain::protocol::proof_blob{.scheme = "test.ancestry"};
      response.audit->state = {
          forge::chain::protocol::proof_blob{.scheme = "test.changes"},
          forge::chain::protocol::proof_blob{.scheme = "test.changes"},
      };
      return response;
   };
   const auto request = forge::chain::protocol::state_changes_request{
       .from_block = 10U,
       .to_block = 12U,
   };
   const auto verify = [&](forge::chain::protocol::state_changes_response response) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };
      return std::pair{run(client.get_changes(request)), std::move(verifier)};
   };

   const auto valid = verify(make_response());
   BOOST_TEST(valid.first.blocks.size() == 2U);
   BOOST_TEST(valid.second->state_change_verifications == 2U);
   BOOST_TEST(valid.second->ancestry_verifications == 1U);
   BOOST_REQUIRE(valid.second->ancestry_finalized);
   BOOST_TEST(valid.second->ancestry_finalized->block == second.block);
   BOOST_REQUIRE_EQUAL(valid.second->ancestry_intermediate.size(), 1U);
   BOOST_TEST(valid.second->ancestry_intermediate.front().block == first.block);
   BOOST_REQUIRE(valid.second->ancestry_proof);
   BOOST_TEST(valid.second->ancestry_proof->scheme == "test.ancestry");

   auto missing_ancestry = make_response();
   missing_ancestry.audit->ancestry.reset();
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(missing_ancestry))),
                     forge::chain::api::exceptions::invalid_finality);

   auto omitted = make_response();
   omitted.blocks.erase(omitted.blocks.begin());
   omitted.audit->state.erase(omitted.audit->state.begin());
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(omitted))), forge::chain::api::exceptions::invalid_state_proof);

   auto stalled = forge::chain::protocol::state_changes_response{};
   stalled.context.anchor = second;
   stalled.next = forge::chain::protocol::state_changes_cursor{.block = 11U};
   stalled.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(stalled))), forge::chain::api::exceptions::invalid_state_proof);

   auto forged_terminal = make_response();
   forged_terminal.blocks.back().anchor.state_root._hash[0] = 99U;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(forged_terminal))),
                     forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(verified_changes_reject_unauthenticated_trailing_ranges_and_cross_block_batches) {
   auto first = forge::chain::protocol::state_anchor{.block_num = 11U};
   first.block._hash[0] = 11U;
   auto finalized = forge::chain::protocol::state_anchor{.block_num = 12U};
   finalized.block._hash[0] = 12U;

   const auto request = forge::chain::protocol::state_changes_request{
       .from_block = 10U,
       .to_block = 12U,
   };
   const auto verify = [&](forge::chain::protocol::state_changes_response response,
                           std::optional<forge::chain::protocol::bytes> next_key = std::nullopt) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      if (next_key) {
         verifier->state_change_result = forge::chain::protocol::state_change_range{.next_key = std::move(next_key)};
      }
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          std::move(verifier),
      };
      return run(client.get_changes(request));
   };
   const auto make_audit = [] {
      return forge::chain::protocol::audit_bundle{
          .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
          .ancestry = forge::chain::protocol::proof_blob{.scheme = "test.ancestry"},
          .state =
              {
                  forge::chain::protocol::proof_blob{.scheme = "test.changes"},
                  forge::chain::protocol::proof_blob{.scheme = "test.changes"},
              },
      };
   };

   auto trailing = forge::chain::protocol::state_changes_response{};
   trailing.context.anchor = finalized;
   trailing.blocks = {{.anchor = first, .ranges = {{.range = {}}, {.range = {}}}}};
   trailing.next = forge::chain::protocol::state_changes_cursor{
       .block = 11U,
       .key = forge::chain::protocol::bytes{0x20U},
   };
   trailing.audit = make_audit();
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(trailing), forge::chain::protocol::bytes{0x20U})),
                     forge::chain::api::exceptions::invalid_state_proof);

   auto crossing = forge::chain::protocol::state_changes_response{};
   crossing.context.anchor = finalized;
   crossing.blocks = {{.anchor = first, .ranges = {{.range = {}}, {.range = {}}}}};
   crossing.audit = make_audit();
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(crossing))),
                     forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(verified_changes_accept_paginated_proof_ranges_without_rewriting_the_requested_range) {
   auto anchor = forge::chain::protocol::state_anchor{.block_num = 11U};
   anchor.block._hash[0] = 11U;
   const auto requested = forge::chain::protocol::key_range{
       .lower = forge::chain::protocol::bytes{0x10U},
       .upper = forge::chain::protocol::bytes{0x40U},
   };
   const auto continuation = forge::chain::protocol::bytes{0x20U};

   auto response = forge::chain::protocol::state_changes_response{};
   response.context.anchor = anchor;
   response.blocks = {{.anchor = anchor, .ranges = {{.range = requested}}}};
   response.audit = forge::chain::protocol::audit_bundle{};
   response.audit->finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   response.audit->state = {forge::chain::protocol::proof_blob{.scheme = "test.changes"}};

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
   auto verifier = std::make_shared<accepting_audit_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
       }},
       verifier,
   };

   const auto result = run(client.get_changes({
       .from_block = 10U,
       .to_block = 11U,
       .ranges = {requested},
       .cursor =
           forge::chain::protocol::state_changes_cursor{
               .block = 11U,
               .key = continuation,
           },
   }));

   BOOST_REQUIRE_EQUAL(result.blocks.size(), 1U);
   BOOST_REQUIRE_EQUAL(result.blocks.front().ranges.size(), 1U);
   BOOST_CHECK(result.blocks.front().ranges.front().range == requested);
   BOOST_TEST(verifier->state_change_verifications == 1U);
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_accepts_real_point_membership_and_nonmembership) {
   const auto fixture = make_authenticated_point_fixture();
   const auto anchor = authenticated_anchor(fixture.root);
   auto finality = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = fixture.domain,
           .proof_limits = {},
       },
       finality,
   };

   const auto membership = authenticated_point_proof(fixture, "alpha");
   const auto membership_value =
       verifier.verify_state_point(anchor, forge::chain::protocol::state_point_request{.key = protocol_bytes("alpha")},
                                   authenticated_proof_blob("forge.db.authenticated.point", membership));
   BOOST_REQUIRE(membership_value.has_value());
   BOOST_CHECK(*membership_value == protocol_bytes("one"));

   const auto nonmembership = authenticated_point_proof(fixture, "beta");
   const auto absent =
       verifier.verify_state_point(anchor, forge::chain::protocol::state_point_request{.key = protocol_bytes("beta")},
                                   authenticated_proof_blob("forge.db.authenticated.point", nonmembership));
   BOOST_TEST(!absent.has_value());
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_rejects_membership_without_value_bytes) {
   const auto fixture = make_authenticated_point_fixture();
   const auto anchor = authenticated_anchor(fixture.root);
   auto verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = fixture.domain,
           .proof_limits = {},
       },
       std::make_shared<recording_finality_verifier>(),
   };

   auto proof = authenticated_point_proof(fixture, "alpha");
   BOOST_REQUIRE(proof.terminal.has_value());
   proof.terminal->value.reset();

   BOOST_CHECK_THROW(static_cast<void>(verifier.verify_state_point(
                         anchor, forge::chain::protocol::state_point_request{.key = protocol_bytes("alpha")},
                         authenticated_proof_blob("forge.db.authenticated.point", proof))),
                     forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_rejects_wrong_scheme_version_limits_chain_and_root) {
   const auto fixture = make_authenticated_point_fixture();
   const auto anchor = authenticated_anchor(fixture.root);
   const auto request = forge::chain::protocol::state_point_request{.key = protocol_bytes("alpha")};
   const auto proof = authenticated_point_proof(fixture, "alpha");
   const auto valid = authenticated_proof_blob("forge.db.authenticated.point", proof);
   auto finality = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = fixture.domain,
           .proof_limits = {},
       },
       finality,
   };

   auto wrong_scheme = valid;
   wrong_scheme.scheme = "forge.db.authenticated.range";
   BOOST_CHECK_THROW(static_cast<void>(verifier.verify_state_point(anchor, request, wrong_scheme)),
                     forge::chain::api::exceptions::invalid_state_proof);

   auto wrong_version = valid;
   ++wrong_version.version;
   BOOST_CHECK_THROW(static_cast<void>(verifier.verify_state_point(anchor, request, wrong_version)),
                     forge::chain::api::exceptions::invalid_state_proof);

   BOOST_REQUIRE(valid.payload.size() > 1U);
   auto tight_limits = authenticated::limits{};
   tight_limits.max_proof_bytes = valid.payload.size() - 1U;
   auto limited = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = fixture.domain,
           .proof_limits = tight_limits,
       },
       std::make_shared<recording_finality_verifier>(),
   };
   BOOST_CHECK_THROW(static_cast<void>(limited.verify_state_point(anchor, request, valid)),
                     forge::chain::api::exceptions::invalid_state_proof);

   auto wrong_root = anchor;
   ++wrong_root.state_root._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verifier.verify_state_point(wrong_root, request, valid)),
                     forge::chain::api::exceptions::invalid_state_proof);

   const auto context = forge::chain::protocol::response_context{.chain = authenticated_chain(), .anchor = anchor};
   BOOST_CHECK_NO_THROW(verifier.verify_context(context));
   auto wrong_context = context;
   ++wrong_context.chain._hash[0];
   BOOST_CHECK_THROW(verifier.verify_context(wrong_context), forge::chain::api::exceptions::wrong_chain);

   const auto finality_proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   BOOST_CHECK_NO_THROW(verifier.verify_finality(anchor, finality_proof));
   BOOST_TEST(finality->verify_calls == 1U);
   auto wrong_chain_anchor = anchor;
   ++wrong_chain_anchor.chain._hash[0];
   BOOST_CHECK_THROW(verifier.verify_finality(wrong_chain_anchor, finality_proof),
                     forge::chain::api::exceptions::wrong_chain);
   BOOST_TEST(finality->verify_calls == 1U);
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_verifies_ranked_range_and_change_tombstone_proofs) {
   const auto ranked = make_authenticated_ranked_fixture();
   auto ranked_verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = ranked.domain,
           .proof_limits = {},
       },
       std::make_shared<recording_finality_verifier>(),
   };
   const auto range = forge::chain::protocol::key_range{.lower = protocol_bytes("b")};
   const auto request = forge::chain::protocol::state_range_request{.range = range, .limit = 2U};
   const auto proof = authenticated_ranked_proof(ranked, authenticated::range_request{
                                                             .lower = authenticated_bytes("b"),
                                                             .limit = 2U,
                                                             .include_values = true,
                                                         });
   const auto result = ranked_verifier.verify_state_range(
       authenticated_anchor(ranked.root), request, authenticated_proof_blob("forge.db.authenticated.range", proof));
   BOOST_REQUIRE_EQUAL(result.rows.size(), 2U);
   BOOST_CHECK(result.rows[0].key == protocol_bytes("b"));
   BOOST_CHECK(result.rows[0].value == protocol_bytes("two"));
   BOOST_CHECK(result.rows[1].key == protocol_bytes("c"));
   BOOST_CHECK(result.rows[1].value == protocol_bytes("three"));
   BOOST_REQUIRE(result.next_key.has_value());
   BOOST_CHECK(*result.next_key == protocol_bytes("d"));

   const auto reverse_request = forge::chain::protocol::state_range_request{
       .range = range,
       .limit = 2U,
       .reverse = true,
   };
   const auto reverse_proof = authenticated_ranked_proof(ranked, authenticated::range_request{
                                                                     .lower = authenticated_bytes("b"),
                                                                     .limit = 2U,
                                                                     .include_values = true,
                                                                     .reverse = true,
                                                                 });
   const auto reverse_result =
       ranked_verifier.verify_state_range(authenticated_anchor(ranked.root), reverse_request,
                                          authenticated_proof_blob("forge.db.authenticated.range", reverse_proof));
   BOOST_REQUIRE_EQUAL(reverse_result.rows.size(), 2U);
   BOOST_CHECK(reverse_result.rows[0].key == protocol_bytes("d"));
   BOOST_CHECK(reverse_result.rows[0].value == protocol_bytes("four"));
   BOOST_CHECK(reverse_result.rows[1].key == protocol_bytes("c"));
   BOOST_CHECK(reverse_result.rows[1].value == protocol_bytes("three"));
   BOOST_REQUIRE(reverse_result.next_key.has_value());
   BOOST_CHECK(*reverse_result.next_key == protocol_bytes("c"));

   const auto changes = make_authenticated_changes_fixture();
   auto changes_verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = changes.domain,
           .proof_limits = {},
       },
       std::make_shared<recording_finality_verifier>(),
   };
   const auto change_range = forge::chain::protocol::key_range{};
   const auto change_proof =
       authenticated_changes_proof(changes, authenticated::range_request{.limit = 2U, .include_values = true});
   const auto change_result =
       changes_verifier.verify_state_changes(authenticated_anchor(changes.root), change_range, 2U,
                                             authenticated_proof_blob("forge.db.authenticated.changes", change_proof));
   BOOST_CHECK(change_result.range == change_range);
   BOOST_REQUIRE_EQUAL(change_result.mutations.size(), 2U);
   BOOST_CHECK(change_result.mutations[0].key == protocol_bytes("alpha"));
   BOOST_TEST(!change_result.mutations[0].value.has_value());
   BOOST_CHECK(change_result.mutations[1].key == protocol_bytes("beta"));
   BOOST_REQUIRE(change_result.mutations[1].value.has_value());
   BOOST_CHECK(*change_result.mutations[1].value == protocol_bytes("updated"));
   BOOST_TEST(!change_result.next_key.has_value());
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_rejects_reordered_and_forged_ranked_inputs) {
   const auto fixture = make_authenticated_ranked_fixture();
   const auto request = forge::chain::protocol::state_range_request{
       .range = {.lower = protocol_bytes("b"), .upper = protocol_bytes("d")},
       .limit = 2U,
   };
   const auto valid = authenticated_ranked_proof(fixture, authenticated::range_request{
                                                              .lower = authenticated_bytes("b"),
                                                              .upper = authenticated_bytes("d"),
                                                              .limit = 2U,
                                                              .include_values = true,
                                                          });
   auto verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = fixture.domain,
           .proof_limits = {},
       },
       std::make_shared<recording_finality_verifier>(),
   };
   const auto reject = [&](const authenticated::range_proof& proof) {
      BOOST_CHECK_THROW(static_cast<void>(verifier.verify_state_range(
                            authenticated_anchor(fixture.root), request,
                            authenticated_proof_blob("forge.db.authenticated.range", proof))),
                        forge::chain::api::exceptions::invalid_state_proof);
   };

   auto reordered = valid;
   std::swap(reordered.nodes[2], reordered.nodes[3]);
   reject(reordered);

   auto forged_rank = valid;
   ++std::get<authenticated::range_inner>(forged_rank.nodes.front()).size;
   reject(forged_rank);

   auto forged_value = valid;
   std::get<authenticated::proof_leaf>(forged_value.nodes[2]).value = authenticated_bytes("forged");
   reject(forged_value);
}

BOOST_AUTO_TEST_CASE(authenticated_audit_verifier_verifies_transaction_merkle_proof_and_rejects_forgery) {
   auto first_id = forge::chain::protocol::transaction_id{};
   first_id._hash[0] = 0x51U;
   auto second_id = forge::chain::protocol::transaction_id{};
   second_id._hash[0] = 0x52U;

   auto first_receipt = forge::chain::protocol::transaction_receipt{};
   first_receipt.status = forge::chain::protocol::transaction_receipt::status::executed;
   first_receipt.cpu_usage_us = 7U;
   first_receipt.trx = first_id;
   auto second_receipt = forge::chain::protocol::transaction_receipt{};
   second_receipt.status = forge::chain::protocol::transaction_receipt::status::executed;
   second_receipt.cpu_usage_us = 8U;
   second_receipt.trx = second_id;

   const auto leaves = std::array{first_receipt.digest(), second_receipt.digest()};
   auto anchor = authenticated_anchor(make_authenticated_point_fixture().root);
   anchor.transaction_root = forge::chain::core::calculate_merkle_root(leaves);
   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = first_id;
   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   response.block = anchor.block;
   response.block_num = anchor.block_num;
   response.receipt = first_receipt;
   const auto proof = forge::chain::protocol::transaction_inclusion_proof{
       .leaf = leaves[0],
       .index = 0U,
       .leaf_count = leaves.size(),
       .path = forge::chain::core::calculate_merkle_path(leaves, 0U),
   };
   auto verifier = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = authenticated_chain(),
           .state_domain = "forge.test.chain-api.transaction-proof.v3",
           .proof_limits = {},
       },
       std::make_shared<recording_finality_verifier>(),
   };

   BOOST_CHECK_NO_THROW(verifier.verify_transaction(anchor, first_id, response, proof));

   auto traced = response;
   traced.trace = forge::chain::protocol::transaction_trace{.id = first_id};
   BOOST_CHECK_THROW(verifier.verify_transaction(anchor, first_id, traced, proof),
                     forge::chain::api::exceptions::invalid_transaction_proof);

   auto reordered = proof;
   reordered.index = 1U;
   BOOST_CHECK_THROW(verifier.verify_transaction(anchor, first_id, response, reordered),
                     forge::chain::api::exceptions::invalid_transaction_proof);

   auto forged_path = proof;
   BOOST_REQUIRE_EQUAL(forged_path.path.size(), 1U);
   ++forged_path.path.front().sibling._hash[0];
   BOOST_CHECK_THROW(verifier.verify_transaction(anchor, first_id, response, forged_path),
                     forge::chain::api::exceptions::invalid_transaction_proof);

   auto forged_receipt = response;
   ++forged_receipt.receipt->cpu_usage_us;
   BOOST_CHECK_THROW(verifier.verify_transaction(anchor, first_id, forged_receipt, proof),
                     forge::chain::api::exceptions::invalid_transaction_proof);
}

BOOST_AUTO_TEST_CASE(content_witness_roundtrips_and_returns_the_authenticated_value) {
   const auto value = forge::chain::protocol::bytes{0x10U, 0x20U, 0x30U};
   const auto other = forge::chain::protocol::bytes{0x40U};
   const auto expected = forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{value});
   const auto other_hash = forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{other});
   const auto audit = forge::chain::protocol::audit_bundle{
       .content =
           {
               {.hash = other_hash, .value = other},
               {.hash = expected, .value = value},
           },
   };

   const auto wire = forge::raw::pack(audit);
   const auto decoded =
       forge::raw::unpack_exact<forge::chain::protocol::audit_bundle>(std::span<const std::uint8_t>{wire});
   BOOST_CHECK(decoded == audit);

   const auto& authenticated_without_size = forge::chain::api::require_content_witness(decoded, expected);
   const auto& authenticated = forge::chain::api::require_content_witness(decoded, expected, value.size());
   BOOST_CHECK(authenticated == value);
   BOOST_CHECK(&authenticated_without_size == &decoded.content[1].value);
   BOOST_CHECK(&authenticated == &decoded.content[1].value);
}

BOOST_AUTO_TEST_CASE(content_witness_rejects_missing_duplicate_digest_and_size_mismatch) {
   const auto value = forge::chain::protocol::bytes{0x10U, 0x20U, 0x30U};
   const auto expected = forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{value});
   const auto valid = forge::chain::protocol::content_witness{.hash = expected, .value = value};

   BOOST_CHECK_THROW(
       static_cast<void>(forge::chain::api::require_content_witness(forge::chain::protocol::audit_bundle{}, expected)),
       forge::chain::api::exceptions::invalid_state_proof);

   const auto duplicate = forge::chain::protocol::audit_bundle{.content = {valid, valid}};
   BOOST_CHECK_THROW(static_cast<void>(forge::chain::api::require_content_witness(duplicate, expected)),
                     forge::chain::api::exceptions::invalid_state_proof);

   const auto malformed = forge::chain::protocol::audit_bundle{
       .content = {{.hash = expected, .value = {0xffU}}},
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::chain::api::require_content_witness(malformed, expected)),
                     forge::chain::api::exceptions::invalid_state_proof);

   const auto wrong_size = forge::chain::protocol::audit_bundle{.content = {valid}};
   BOOST_CHECK_THROW(
       static_cast<void>(forge::chain::api::require_content_witness(wrong_size, expected, value.size() + 1U)),
       forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_reuses_an_exact_anchor) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   verifier.verify(anchor, proof);
   verifier.verify(anchor, proof);

   BOOST_TEST(delegate->verify_calls == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_single_flights_a_concurrent_exact_anchor) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<blocking_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   auto first = std::async(std::launch::async, [&] { verifier.verify(anchor, proof); });
   delegate->wait_until_entered();

   auto second_started = std::promise<void>{};
   auto second_started_future = second_started.get_future();
   auto second = std::async(std::launch::async, [&] {
      second_started.set_value();
      verifier.verify(anchor, proof);
   });
   second_started_future.wait();
   const auto second_status = second.wait_for(std::chrono::milliseconds{100});

   delegate->release();
   first.get();
   second.get();

   BOOST_CHECK(second_status == std::future_status::timeout);
   BOOST_TEST(delegate->verify_calls.load() == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_rejects_a_conflicting_anchor_identity) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};
   verifier.verify(anchor, proof);

   auto conflicting = anchor;
   ++conflicting.change_count;
   BOOST_CHECK_THROW(verifier.verify(conflicting, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_TEST(delegate->verify_calls == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_does_not_cache_a_failed_verification) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   delegate->failures_remaining = 1U;
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   BOOST_CHECK_THROW(verifier.verify(anchor, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_NO_THROW(verifier.verify(anchor, proof));
   BOOST_CHECK_NO_THROW(verifier.verify(anchor, proof));
   BOOST_TEST(delegate->verify_calls == 2U);
}

BOOST_AUTO_TEST_CASE(chain_audit_translates_standard_finality_delegate_failures) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<standard_throwing_finality_verifier>();
   auto cached = forge::chain::api::cached_finality_verifier{delegate, 4U};

   BOOST_CHECK_THROW(cached.verify(anchor, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_THROW(cached.verify_ancestry(anchor, {}, proof), forge::chain::api::exceptions::invalid_finality);

   auto authenticated = forge::chain::api::authenticated_audit_verifier{
       {
           .chain = anchor.chain,
           .state_domain = "test.state",
       },
       std::move(delegate),
   };
   BOOST_CHECK_THROW(authenticated.verify_finality(anchor, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_THROW(authenticated.verify_ancestry(anchor, {}, proof), forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(chain_audit_translates_nonstandard_finality_delegate_failures) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   delegate->throw_nonstandard = true;
   auto cached = forge::chain::api::cached_finality_verifier{delegate, 4U};

   BOOST_CHECK_THROW(cached.verify(anchor, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_THROW(cached.verify_ancestry(anchor, {}, proof), forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_delegates_ancestry_and_caches_the_finalized_anchor) {
   const auto finalized = make_finality_anchor();
   auto earlier = finalized;
   earlier.block._hash[0] = 1U;
   earlier.block_num = finalized.block_num - 1U;
   const auto intermediate = std::vector{earlier};
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.ancestry", .version = 1U};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   verifier.verify_ancestry(finalized, std::span<const forge::chain::protocol::state_anchor>{intermediate}, proof);
   verifier.verify(finalized, proof);
   verifier.verify_ancestry(finalized, std::span<const forge::chain::protocol::state_anchor>{intermediate}, proof);

   BOOST_TEST(delegate->verify_calls == 0U);
   BOOST_TEST(delegate->ancestry_calls == 2U);
   BOOST_REQUIRE(delegate->ancestry_finalized);
   BOOST_TEST(delegate->ancestry_finalized->block == finalized.block);
   BOOST_REQUIRE_EQUAL(delegate->ancestry_intermediate.size(), 1U);
   BOOST_TEST(delegate->ancestry_intermediate.front().block == earlier.block);
   BOOST_REQUIRE(delegate->ancestry_proof);
   BOOST_TEST(delegate->ancestry_proof->scheme == proof.scheme);
}
