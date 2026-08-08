module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.db.authenticated.transaction;

import forge.db.authenticated.exceptions;
import forge.db.authenticated.hash;
import forge.db.authenticated.tree_engine;
import forge.db.core.driver;
import forge.db.core.record;

#include "details/transaction_impl.hxx"
#include "details/transaction_participant_impl.hxx"
#include "details/backend_call.hxx"

namespace forge::db::authenticated {

namespace {

void ensure_active(const auto& value) {
   if (!value || !value->active || !value->active->active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "authenticated transaction is closed");
   }
}

detail::get_record_fn transaction_reader(auto& value) {
   return [&value](forge::db::core::record_key key) -> boost::asio::awaitable<std::optional<bytes>> {
      co_return co_await detail::invoke_backend([&] { return value.active->get(value.family, std::move(key)); });
   };
}

struct computation {
   std::vector<mutation> normalized;
   detail::tree_engine state;
   detail::tree_engine changes;
   staged_version staged;
};

boost::asio::awaitable<computation> compute(auto& value, std::span<const mutation> mutations) {
   auto normalized = detail::normalize_mutations(mutations, value.bounds);
   auto read = transaction_reader(value);
   auto state = detail::tree_engine{
       canonical_tree_domain(value.domain, proof_tree::state),
       value.base_root && value.base_root->state_size != 0 ? std::optional<digest>{value.base_root->state_root}
                                                           : std::nullopt,
       read,
       value.bounds,
   };
   const auto state_result = co_await state.apply(normalized);

   auto change_mutations = std::vector<mutation>{};
   change_mutations.reserve(normalized.size());
   for (const auto& item : normalized) {
      change_mutations.push_back(mutation{
          .key = item.key,
          .value = encode_change_value(item),
      });
   }
   auto changes = detail::tree_engine{
       canonical_tree_domain(value.domain, proof_tree::changes),
       std::nullopt,
       read,
       value.bounds,
   };
   const auto change_result = co_await changes.apply(change_mutations);

   auto staged = staged_version{
       .commitment =
           root{
               .version = value.candidate,
               .state_root = state_result.hash,
               .state_size = state_result.size,
               .change_root = change_result.hash,
               .change_count = normalized.size(),
           },
       .mutation_digest = hash_mutations(normalized),
   };
   co_return computation{
       .normalized = std::move(normalized),
       .state = std::move(state),
       .changes = std::move(changes),
       .staged = std::move(staged),
   };
}

bool same_root(const std::optional<root>& left, const std::optional<bytes>& encoded_right) {
   if (!left) {
      return !encoded_right;
   }
   return encoded_right && detail::decode_root(*encoded_right) == *left;
}

} // namespace

transaction::transaction(std::shared_ptr<impl> implementation) : impl_{std::move(implementation)} {}

transaction detail::transaction_access::make(forge::db::core::transaction& active, forge::db::core::family family,
                                             std::string domain, digest namespace_hash, limits bounds,
                                             version_id_t candidate, std::optional<root> base) {
   auto implementation = std::make_shared<transaction::impl>(active, std::move(family), std::move(domain),
                                                             namespace_hash, bounds, candidate, std::move(base));
   active.attach_participant(implementation->participant);
   return transaction{std::move(implementation)};
}

transaction::~transaction() = default;
transaction::transaction(transaction&&) noexcept = default;
transaction& transaction::operator=(transaction&&) noexcept = default;

version_id_t transaction::version() const {
   ensure_active(impl_);
   return impl_->candidate;
}

std::optional<root> transaction::base() const {
   ensure_active(impl_);
   return impl_->base_root;
}

std::optional<staged_version> transaction::staged() const {
   ensure_active(impl_);
   return impl_->participant->staged();
}

boost::asio::awaitable<staged_version> transaction::preview(std::span<const mutation> mutations) {
   ensure_active(impl_);
   auto computed = co_await compute(*impl_, mutations);
   co_return computed.staged;
}

boost::asio::awaitable<staged_version> transaction::stage(std::span<const mutation> mutations,
                                                          std::optional<digest> expected_state_root) {
   ensure_active(impl_);
   if (impl_->participant->staged()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_version, "authenticated transaction is already staged");
   }

   auto computed = co_await compute(*impl_, mutations);
   if (expected_state_root && computed.staged.commitment.state_root != *expected_state_root) {
      FORGE_THROW_EXCEPTION(exceptions::root_mismatch, "authenticated state root does not match the expected root",
                            forge::exceptions::ctx("expected", expected_state_root->str()),
                            forge::exceptions::ctx("actual", computed.staged.commitment.state_root.str()));
   }

   const auto current = co_await detail::invoke_backend(
       [&] { return impl_->active->get_for_update(impl_->family, detail::latest_key(impl_->namespace_hash)); });
   if (!same_root(impl_->base_root, current)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_version, "authenticated state head changed while staging");
   }
   const auto version_key = detail::version_key(impl_->namespace_hash, impl_->candidate);
   if (co_await detail::invoke_backend([&] { return impl_->active->get(impl_->family, version_key); })) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_version, "authenticated state version already exists",
                            forge::exceptions::ctx("version", impl_->candidate));
   }

   auto put = [active = impl_->active, family = impl_->family](forge::db::core::record_key key,
                                                               bytes value) -> boost::asio::awaitable<void> {
      co_await detail::invoke_backend([&] { return active->put(family, std::move(key), std::move(value)); });
   };
   auto get = [active = impl_->active, family = impl_->family](
                  forge::db::core::record_key key) -> boost::asio::awaitable<std::optional<bytes>> {
      co_return co_await detail::invoke_backend([&] { return active->get_for_update(family, std::move(key)); });
   };
   auto erase = [active = impl_->active,
                 family = impl_->family](forge::db::core::record_key key) -> boost::asio::awaitable<void> {
      co_await detail::invoke_backend([&] { return active->erase(family, std::move(key)); });
   };
   co_await computed.state.persist(get, put, erase);
   co_await computed.changes.persist(get, put, erase);
   if (computed.staged.commitment.state_size != 0) {
      co_await detail::retain_root(get, put, erase, computed.staged.commitment.state_root);
   }
   if (computed.staged.commitment.change_count != 0) {
      co_await detail::retain_root(get, put, erase, computed.staged.commitment.change_root);
   }

   const auto encoded = detail::encode_root(computed.staged.commitment);
   co_await detail::invoke_backend([&] { return impl_->active->put(impl_->family, version_key, encoded); });
   co_await detail::invoke_backend(
       [&] { return impl_->active->put(impl_->family, detail::latest_key(impl_->namespace_hash), encoded); });

   impl_->participant->set_staged(computed.staged);
   co_return computed.staged;
}

} // namespace forge::db::authenticated
